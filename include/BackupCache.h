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
        map<unsigned int, BackupEntry> rawData;
        map<string, set<unsigned int> > indexByMD5;
        map<string, unsigned int> indexByFilename;
        bool updated;
        string inProcess;
        string cacheFilename;

        BackupEntry* getByFilename(string filename);
        set<BackupEntry*> getByMD5(string md5);
        void addOrUpdate(BackupEntry updatedEntry, bool markCurrent = false, bool md5Updated = false);
        void updateAges(time_t refTime = 0);
        void reStatMD5(string md5);
        void remove(BackupEntry oldEntry);

        string size();
        string size(string md5);
        string fullDump();

        void saveCache();
        void restoreCache();
        void saveAtEnd(bool shouldSave = 1) { updated = shouldSave; }

        BackupCache(string filename);
        BackupCache();
        ~BackupCache();
};

#endif

