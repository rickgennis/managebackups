#include <iostream>
#include <map>
#include <string>
#include <set>
#include <fstream>
#include <sys/stat.h>

#include "BackupEntry.h"
#include "BackupCache.h"
#include "util_generic.h"
#include "globals.h"
#include "colors.h"

using namespace std;

BackupCache::BackupCache(string filename) {
    BackupCache();
    cacheFilename = filename;
}

BackupCache::BackupCache() {
    updated = false;
    inProcess = "";
}

BackupCache::~BackupCache() {
    if (updated && cacheFilename.length())
        saveCache();
}


    void BackupCache::updateAges(time_t refTime) {
        for (auto &raw: rawData)
            raw.second.updateAges(refTime);
    }


    void BackupCache::saveCache() {
        ofstream cacheFile;

        if (!rawData.size() || !cacheFilename.length())
            return;

        cacheFile.open(cacheFilename);
        if (cacheFile.is_open()) {
            unsigned int count = 0;

            // write raw data
            for (auto &raw: rawData) {

                // files need to be able to fall out of the cache if they disappear from the filesystem.
                // "current" means the file was seen in the most recent filesystem scan.
                if (raw.second.current) {
                    DEBUG(8, cacheFilename << ": writing cache entry " << raw.second.class2string());
                    cacheFile << raw.second.class2string() << endl;
                    ++count;
                }
            }

            cacheFile.close();
            DEBUG(2, "cache saved to " << cacheFilename << " (" << count << " entries)");
        }
        else {
            log("unable to save cache to " + cacheFilename);

            if (!GLOBALS.saveErrorSeen) {
                GLOBALS.saveErrorSeen = true;

                SCREENERR("warning: unable to save cache to disk (" << cacheFilename << "\n" <<
                        "isn't writable); MD5s will continue to be recalculated until corrected.");
            }
        }
    }


    void BackupCache::restoreCache() {
        ifstream cacheFile;

        cacheFile.open(cacheFilename);
        if (cacheFile.is_open()) {

            unsigned int count = 0;
            string cacheData;
            while (getline(cacheFile, cacheData)) {
                ++count;
                BackupEntry entry;
                if (entry.string2class(cacheData))
                    addOrUpdate(entry);
            }

            cacheFile.close();
            DEBUG(2, "loaded " << count << " cache entries from " << cacheFilename);
        }
        else
            log("unable to read cache file " + cacheFilename);
    }


    BackupEntry* BackupCache::getByFilename(string filename) {
        auto filename_it = indexByFilename.find(filename);
        if (filename_it != indexByFilename.end()) {
            auto raw_it = rawData.find(filename_it->second);
            if (raw_it != rawData.end()) {
                return &raw_it->second;
            }
        }

        return(NULL);
    }

    set<BackupEntry*> BackupCache::getByMD5(string md5) {
        set<BackupEntry*> result;

        auto md5_it = indexByMD5.find(md5);
        if (md5_it != indexByMD5.end()) {
            for (auto &fileID: md5_it->second) {
                auto raw_it = rawData.find(fileID);
                if (raw_it != rawData.end()) {
                    result.insert(&raw_it->second);
                }
            }
        }        
        return result;
    }

    void BackupCache::addOrUpdate(BackupEntry updatedEntry, bool markCurrent, bool md5Updated) {
        auto filename_it = indexByFilename.find(updatedEntry.filename);
        updatedEntry.current = markCurrent;

        if (md5Updated)
            updated = true;

        // filename doesn't exist
        if (filename_it == indexByFilename.end()) {
            unsigned int index = rawData.rbegin() == rawData.rend() ? 1 : rawData.rbegin()->first + 1;

            // add rawData
            rawData.insert(rawData.end(), pair<unsigned int, BackupEntry>(index, updatedEntry));

            // update filename index
            indexByFilename.insert(indexByFilename.end(), pair<string, unsigned int>(updatedEntry.filename, index));

            // update MD5 index
            auto md5_it = indexByMD5.find(updatedEntry.md5);
            set<unsigned int> md5_list;

            // is there an index (set) of files for this MD5?
            if (md5_it != indexByMD5.end()) {
                auto set_it = md5_it->second.find(index);
                md5_it->second.insert(index);
            }
            else {
                set<unsigned int> newSet; 
                newSet.insert(index);
                indexByMD5.insert(indexByMD5.end(), pair<string, set<unsigned int> >(updatedEntry.md5, newSet));
            }
        }
        // filename does exist and we're updating it's data
        else {
            unsigned int index = filename_it->second;
            auto raw_it = rawData.find(index);
            if (raw_it != rawData.end()) {
                auto backupEntry = raw_it->second;
                string oldMD5 = backupEntry.md5;

                // update the rawData entry
                raw_it->second = updatedEntry;
                DEBUG(4, "updated raw data for " << backupEntry.filename);

                // if the md5 changed...
                if (updatedEntry.md5 != oldMD5) {
                    auto md5_it = indexByMD5.find(oldMD5);

                    // if the old one is in the index, remove it
                    if (md5_it != indexByMD5.end()) {
                        md5_it->second.erase(index);
                        DEBUG(4, "(" << markCurrent << ") removed reference from old md5 (" << oldMD5 << ") to " << backupEntry.filename);

                        if (!md5_it->second.size()) {
                            indexByMD5.erase(oldMD5);
                            DEBUG(4, "(" << markCurrent << ") dropping old md5 " << oldMD5 << ",  as " << backupEntry.filename << " was the last reference");
                        }
                    }

                    // if the new md5 isn't in the index, add it
                    md5_it = indexByMD5.find(updatedEntry.md5);
                    if (md5_it != indexByMD5.end()) {
                        md5_it->second.insert(index);
                    }
                    // if the md5 is already there, then just add this file to its list
                    else {
                        set<unsigned int> newSet;
                        newSet.insert(index);
                        indexByMD5.insert(indexByMD5.end(), pair<string, set<unsigned int> >(updatedEntry.md5, newSet));
                    }
                }
            }
            else {
                // entry is in the filenameIndex but not the raw data.  should never get here.
                SCREENERR("LOGIC ERROR - NO RAW DATA");
                exit(1);
            }
        }
    }


    void BackupCache::remove(BackupEntry oldEntry) {
        // find the entry in the filename index
        auto filename_it = indexByFilename.find(oldEntry.filename);
        if (filename_it != indexByFilename.end()) {
            unsigned int index = filename_it->second;
            indexByFilename.erase(oldEntry.filename);    // and remove it
            DEBUG(5, "removed " << oldEntry.filename << " from filename index");

            // find the entry in the raw data
            auto raw_it = rawData.find(index);
            if (raw_it != rawData.end()) {
                string fileMD5 = raw_it->second.md5;
                rawData.erase(index);                    // and remove it
                DEBUG(5, "removed " << index << " from raw data");

                    // find the entry in the md5 index
                    auto md5_it = indexByMD5.find(fileMD5);
                    if (md5_it != indexByMD5.end()) {
                        md5_it->second.erase(index);     // and remove it
                        DEBUG(5, "removed " << fileMD5 << " from md5 index");

                        if (!md5_it->second.size()) {      // if that was the last/only file with that MD5
                            indexByMD5.erase(fileMD5);    // then remove that MD5 entirely from the index
                            DEBUG(5, "removed final reference to " << fileMD5);
                        }
                    }
            }
        }
    }


    string BackupCache::size() {
        return (to_string(rawData.size()) + string(" cache entries, ") + 
                to_string(indexByFilename.size()) + string(" filename index, ") + 
                to_string(indexByMD5.size()) + string(" MD5 index"));
    }

    string BackupCache::size(string md5) {
        auto md5_it = indexByMD5.find(md5);
        if (md5_it != indexByMD5.end()) {
            return(md5_it->first + ": " + to_string(md5_it->second.size()));
        }
        return (md5 + ": 0");
    }

    string BackupCache::fullDump() {
        string result ("RAW Data\n");
        for (auto &raw: rawData) {
            result += "\tid:" + to_string(raw.first) + 
                ", file:" + raw.second.filename + 
                ", md5:" + raw.second.md5 + 
                ", size:" + to_string(raw.second.size) + 
                ", inod:" + to_string(raw.second.inode) + 
                ", dage:" + to_string(raw.second.day_age) + 
                ", dow:" + dw(raw.second.dow) + 
                ", day:" + to_string(raw.second.date_day) + 
                ", lnks:" + to_string(raw.second.links) + 
                ", mtim:" + to_string(raw.second.mtime) + "\n";
        }

        result += "\nFilename Index\n";
        for (auto &filename: indexByFilename) {
            result += "\t" + filename.first + ": " + to_string(filename.second) + "\n";
        }

        result += "\nMD5 Index\n";
        for (auto &md5: indexByMD5) {
            result += "\t" + md5.first + ": ";

            string detail;
            for (auto &fileID: md5.second) {
                detail += string(detail.length() > 0 ? ", " : "") + to_string(fileID);
            }

            result += detail + "\n";
        }

        return result;
    }


void BackupCache::reStatMD5(string md5) {
    struct stat statBuf;

    auto md5_it = indexByMD5.find(md5);
    if (md5_it != indexByMD5.end()) {

        for (auto &fileID: md5_it->second) {
            auto raw_it = rawData.find(fileID);

            if (raw_it != rawData.end()) {
                ++GLOBALS.statsCount;
                if (!stat(raw_it->second.filename.c_str(), &statBuf)) {
                    raw_it->second.links = statBuf.st_nlink;
                    raw_it->second.mtime = statBuf.st_mtime;
                    raw_it->second.inode = statBuf.st_ino;
                    DEBUG(5, "restat: " << raw_it->second.filename << " (links " << raw_it->second.links << ")");
                }
            }
        }
    }        
}



