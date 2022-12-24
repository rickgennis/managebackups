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

        bool _autoSave;
        string cacheFilename;
        map<string, FaubEntry> backups;

    public:
        void saveCache();
        void restoreCache();

        FaubCache(string path, bool autoSave = true);
        FaubCache();
        ~FaubCache();
};

#endif
