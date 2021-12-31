#ifndef BCACHE_H
#define BCACHE_H

#include <iostream>
#include <map>
#include <string>
#include <set>

#include "BackupEntry.h"

using namespace std;

class BackupCache {
    public:
        map<int, BackupEntry> rawData;
        map<string, set<int> > indexByMD5;
        map<string, int> indexByFilename;
        bool scanned;

        string cacheFilename;
        BackupEntry* getByFilename(string filename);
        set<BackupEntry*> getByMD5(string md5);
        void addOrUpdate(BackupEntry updatedEntry, bool markCurrent = 0);

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

