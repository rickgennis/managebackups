#include <iostream>
#include <map>
#include <string>
#include <set>
#include <fstream>
#include <sys/stat.h>

#include "BackupEntry.h"
#include "BackupCache.h"
#include "util.h"
#include "globals.h"
#include "colors.h"

using namespace std;

BackupCache::BackupCache(string filename) {
    BackupCache();
    cacheFilename = filename;
}

BackupCache::BackupCache() {
    scanned = false;
}

BackupCache::~BackupCache() {
    if (scanned && cacheFilename.length())
        saveCache();
}


    void BackupCache::updateAges(time_t refTime) {
        for (auto raw_it = rawData.begin(); raw_it != rawData.end(); ++raw_it) 
            raw_it->second.updateAges(refTime);
    }


    void BackupCache::saveCache() {
        ofstream cacheFile;

        cacheFile.open(cacheFilename);
        if (cacheFile.is_open()) {

            // write raw data
            for (auto raw_it = rawData.begin(); raw_it != rawData.end(); ++raw_it) {

                // files need to be able to file out of the cache if they disappear from the filesystem.
                // "current" means the file was seen in the most recent filesystem scan.
                if (raw_it->second.current)
                    cacheFile << raw_it->second.class2string() << endl;
            }

            cacheFile.close();
        }
        else
            log("unable to save cache to " + cacheFilename);
    }


    void BackupCache::restoreCache() {
        ifstream cacheFile;

        cacheFile.open(cacheFilename);
        if (cacheFile.is_open()) {

            string cacheData;
            while (getline(cacheFile, cacheData)) {
                BackupEntry entry;
                entry.string2class(cacheData);
                addOrUpdate(entry);
            }

            cacheFile.close();
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
            for (auto set_it = md5_it->second.begin(); set_it != md5_it->second.end(); ++set_it) {
                auto raw_it = rawData.find(*set_it);
                if (raw_it != rawData.end()) {
                    result.insert(&raw_it->second);
                }
            }
        }        
        return result;
    }

    void BackupCache::addOrUpdate(BackupEntry updatedEntry, bool markCurrent) {
        auto filename_it = indexByFilename.find(updatedEntry.filename);
        updatedEntry.current = markCurrent;

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
                        DEBUG(4, "removed reference from old md5 (" << oldMD5 << ") to " << backupEntry.filename);

                        if (!md5_it->second.size()) {
                            indexByMD5.erase(oldMD5);
                            DEBUG(4, "dropping old md5 (" << oldMD5 << ") as " << backupEntry.filename << " was the last reference");
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
                cerr << RED << "LOGIC ERROR - NO RAW DATA" << RESET << endl;
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
            DEBUG(4, ": removed " << oldEntry.filename << " from filename index");

            // find the entry in the raw data
            auto raw_it = rawData.find(index);
            if (raw_it != rawData.end()) {
                string fileMD5 = raw_it->second.md5;
                rawData.erase(index);                    // and remove it
                DEBUG(4, ": removed " << index << " from raw data");

                    // find the entry in the md5 index
                    auto md5_it = indexByMD5.find(fileMD5);
                    if (md5_it != indexByMD5.end()) {
                        md5_it->second.erase(index);     // and remove it
                        DEBUG(4, ": removed " << fileMD5 << " from md5 index");

                        if (!md5_it->second.size())      // if that was the last/only file with that MD5
                            indexByMD5.erase(fileMD5);   // then remove that MD5 entirely from the index
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
        for (auto raw_it = rawData.begin(); raw_it != rawData.end(); ++raw_it) {
            result += "\tid:" + to_string(raw_it->first) + 
                ", file:" + raw_it->second.filename + 
                ", md5:" + raw_it->second.md5 + 
                ", size:" + to_string(raw_it->second.size) + 
                ", inod:" + to_string(raw_it->second.inode) + 
                ", dage:" + to_string(raw_it->second.day_age) + 
                ", mage:" + to_string(raw_it->second.month_age) + 
                ", dow:" + dw(raw_it->second.dow) + 
                ", day:" + to_string(raw_it->second.date_day) + 
                ", lnks:" + to_string(raw_it->second.links) + 
                ", mtim:" + to_string(raw_it->second.mtime) + "\n";
        }

        result += "\nFilename Index\n";
        for (auto filename_it = indexByFilename.begin(); filename_it != indexByFilename.end(); ++filename_it) {
            result += "\t" + filename_it->first + ": " + to_string(filename_it->second) + "\n";
        }

        result += "\nMD5 Index\n";
        for (auto md5_it = indexByMD5.begin(); md5_it != indexByMD5.end(); ++md5_it) {
            result += "\t" + md5_it->first + ": ";

            string detail;
            for (auto set_it = md5_it->second.begin(); set_it != md5_it->second.end(); ++set_it) {
                detail += string(detail.length() > 0 ? ", " : "") + to_string(*set_it);
            }

            result += detail + "\n";
        }

        return result;
    }


void BackupCache::reStatMD5(string md5) {
    struct stat statBuf;

    auto md5_it = indexByMD5.find(md5);
    if (md5_it != indexByMD5.end()) {

        for (auto set_it = md5_it->second.begin(); set_it != md5_it->second.end(); ++set_it) {
            auto raw_it = rawData.find(*set_it);

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



