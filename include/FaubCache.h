#ifndef FCACHE_H
#define FCACHE_H

#include <string>
#include <set>
#include <map>
#include <sys/stat.h>
#include "BaseCache.h"
#include "FaubEntry.h"


using namespace std;

class FaubCache : public BaseCache {
    private:
        string baseDir;
        map<string, FaubEntry> backups;

    public:
        void restoreCache(string profileName);

        map<string, FaubEntry>::iterator getBackupByDir(string dir) { return backups.find(dir); }
        void recache(string dir);

        FaubCache(string path, string profileName);
        ~FaubCache();
};

#endif
