#ifndef FCACHE_H
#define FCACHE_H

#include <string>
#include <set>
#include <map>
#include <sys/stat.h>
#include "FaubEntry.h"


using namespace std;

class FaubCache {
    private:
        string baseDir;
        map<string, FaubEntry> backups;

    public:
        void restoreCache(string profileName);
        tuple<size_t, size_t> getTotalStats();
        long size() { return backups.size(); }

        map<string, FaubEntry>::iterator getBackupByDir(string dir) { return backups.find(dir); }
        map<string, FaubEntry>::iterator getFirstBackup() { return (backups.begin()); }
        map<string, FaubEntry>::iterator getLastBackup() { return (backups.size() ? --backups.end() : backups.end()); }
        map<string, FaubEntry>::iterator getEnd() { return backups.end(); }

        void recache(string dir);

        FaubCache(string path, string profileName);
        ~FaubCache();
};

#endif
