#ifndef BCACHE_H
#define BCACHE_H

#include <iostream>
#include <map>
#include <string>
#include <set>

#include "BackupEntry.h"

using namespace std;

class BackupCache {
    map<int, BackupEntry> rawData;
    map<string, set<int> > indexByMD5;
    map<string, int> indexByFilename;
    bool modified;

    public:
        string cacheFilename;
        BackupEntry* getByFilename(string filename);
        set<BackupEntry*> getByMD5(string md5);
        void addOrUpdate(BackupEntry updatedEntry);

        string size();
        string size(string md5);
        string fullDump();

        void updateAges(time_t refTime = 0);
        void saveCache();
        void restoreCache();

        BackupCache(string filename);
        BackupCache();
        ~BackupCache();
};

#endif

