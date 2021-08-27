#include <iostream>
#include <map>
#include <string>
#include <set>
using namespace std;

#include "BackupEntry.h"
#include "BackupCache.h"

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

    void BackupCache::addOrUpdate(BackupEntry updatedEntry) {
        auto filename_it = indexByFilename.find(updatedEntry.filename);

        // filename doesn't exist
        if (filename_it == indexByFilename.end()) {
            int index = rawData.rbegin() == rawData.rend() ? 1 : rawData.rbegin()->first + 1;

            // add rawData
            rawData.insert(rawData.end(), pair<int, BackupEntry>(index, updatedEntry));

            // update filename index
            indexByFilename.insert(indexByFilename.end(), pair<string, int>(updatedEntry.filename, index));

            // update MD5 index
            auto md5_it = indexByMD5.find(updatedEntry.md5);
            set<int> md5_list;

            // is there an index (set) of files for this MD5?
            if (md5_it != indexByMD5.end()) {
                auto set_it = md5_it->second.find(index);
                md5_it->second.insert(index);
            }
            else {
                set<int> newSet; 
                newSet.insert(index);
                indexByMD5.insert(indexByMD5.end(), pair<string, set<int> >(updatedEntry.md5, newSet));
            }
        }
        // filename does exist and we're updating it's data
        else {
            int index = filename_it->second;
            auto raw_it = rawData.find(index);
            if (raw_it != rawData.end()) {
                auto backupEntry = raw_it->second;
                string oldMD5 = backupEntry.md5;

                // update the rawData entry
                raw_it->second = updatedEntry;
                cout << "updated raw data for " << backupEntry.filename << endl;

                // if the md5 changed...
                if (updatedEntry.md5 != oldMD5) {
                    auto md5_it = indexByMD5.find(oldMD5);

                    // if the old one is in the index, remove it
                    if (md5_it != indexByMD5.end()) {
                        md5_it->second.erase(index);
                        cout << "removed reference from old md5 (" << oldMD5 << ") to " << backupEntry.filename << endl;
                    }

                    // if the new one isn't in the index, add it
                    md5_it = indexByMD5.find(updatedEntry.md5);
                    if (md5_it != indexByMD5.end()) {
                        md5_it->second.insert(index);
                    }
                    else {
                        set<int> newSet;
                        newSet.insert(index);
                        indexByMD5.insert(indexByMD5.end(), pair<string, set<int> >(updatedEntry.md5, newSet));
                    }
                }
            }
            else {
                // entry is in the filenameIndex but not the raw data.  should never get here.
                cout << "LOGIC ERROR - NO RAW DATA" << endl;
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
                ", fln:" + raw_it->second.filename + 
                ", md5:" + raw_it->second.md5 + 
                ", byt:" + to_string(raw_it->second.bytes) + 
                ", ind:" + to_string(raw_it->second.inode) + 
                ", age:" + to_string(raw_it->second.age) + 
                ", mth:" + to_string(raw_it->second.month_age) + 
                ", dow:" + to_string(raw_it->second.dow) + 
                ", day:" + to_string(raw_it->second.day) + 
                ", lnk:" + to_string(raw_it->second.links) + 
                ", mtm:" + to_string(raw_it->second.mtime) + "\n";
        }

        result += "\nFilename Index\n";
        for (auto filename_it = indexByFilename.begin(); filename_it != indexByFilename.end(); ++filename_it) {
            result += "\t" + filename_it->first + ": " + to_string(filename_it->second) + "\n";
        }

        result += "\nMD5 Index\n";
        for (auto md5_it = indexByMD5.begin(); md5_it != indexByMD5.end(); ++md5_it) {
            for (auto set_it = md5_it->second.begin(); set_it != md5_it->second.end(); ++set_it) {
                result += "\t" + md5_it->first + ": " + to_string(*set_it) + "\n";
            }
        }

        return result;
    }

