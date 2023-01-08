
#include <fstream>
#include <algorithm>
#include <queue>
#include <dirent.h>
#include "globals.h"
#include "FaubCache.h"
#include "BackupConfig.h"
#include "util_generic.h"
#include "debug.h"

#include "FaubCache.h"


FaubCache::FaubCache(string path, string profileName) {
    baseDir = path;
    restoreCache(profileName);
}


FaubCache::~FaubCache() {
}


void FaubCache::restoreCache(string profileName) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    queue<string> dirQueue;
    string currentDir;

    dirQueue.push(baseDir);
    auto baseSlashes = count(baseDir.begin(), baseDir.end(), '/');

    while (dirQueue.size()) {
        currentDir = dirQueue.front();
        dirQueue.pop();

        if ((c_dir = opendir(currentDir.c_str())) != NULL) {
            while ((c_dirEntry = readdir(c_dir)) != NULL) {

                if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                    continue;

                ++GLOBALS.statsCount;
                struct stat statData;
                string fullFilename = slashConcat(currentDir, c_dirEntry->d_name);

                if (!stat(fullFilename.c_str(), &statData)) {

                    if ((statData.st_mode & S_IFMT) == S_IFDIR) {
                        // regardless of the starting (baseDir) directory, we're only interested in subdirs
                        // exactly 2 levels lower because that's where our backups will live. e.g.
                        // baseDir = /tmp/backups then we're looking for things like /tmp/backups/2023/01.
                        if (count(fullFilename.begin(), fullFilename.end(), '/') - baseSlashes == 3) {

                            // next we make sure the subdir matches our profile name
                            if (fullFilename.find(profileName) != string::npos) {
                                FaubEntry entry(fullFilename);
                                auto success = entry.loadStats();
                                DEBUG(D_faub) DFMT("loading cache for " << fullFilename << (success ? ": success" : ": failed"));
                                backups.insert(backups.end(), pair<string, FaubEntry>(fullFilename, entry));
                                continue;
                            }
                        }

                        dirQueue.push(fullFilename);
                    }
                }
            }
        }
    }

    // all backups are loaded; now see which are missing stats and recache them
    recache("");
}


void FaubCache::recache(string dir) {
    map<string, FaubEntry>::iterator prevBackup = backups.end();

    for (auto aBackup = backups.begin(); aBackup != backups.end(); ++aBackup) {
        DEBUG(D_faub) DFMT("cache set has " << aBackup->first << " with size " << aBackup->second.totalSize << ", duration " << aBackup->second.duration);

        if (!aBackup->second.totalSize ||                // if we have no cached data for this backup or
            ((dir.length() && dir == aBackup->first) &&  // (this is a backup we've specifically been asked to recache
            !aBackup->second.updated)) {                 // and it hasn't already been updated on this run of the app)

            bool gotPrev = prevBackup != backups.end();
            if (gotPrev)
                prevBackup->second.loadInodes();

            set<ino_t> emptySet;
            auto [totalSize, totalSaved] = dus(aBackup->first, gotPrev ? prevBackup->second.inodes : emptySet, aBackup->second.inodes);
            DEBUG(D_faub) DFMT("dus(" << aBackup->first << ") returned " << totalSize << " bytes");
            aBackup->second.totalSize = totalSize;
            aBackup->second.totalSaved = totalSaved;
            aBackup->second.updated = true;
        }

        prevBackup = aBackup;
    }
}


tuple<size_t, size_t> FaubCache::getTotalStats() {
    size_t totalSize = 0;
    size_t totalSaved = 0;

    for (auto &aBackup: backups) {
        totalSize += aBackup.second.totalSize;
        totalSaved += aBackup.second.totalSaved;
    }

    return {totalSize, totalSaved};
}


