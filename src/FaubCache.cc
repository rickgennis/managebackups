
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
    cerr << "*** FaubCache setup for " << path << " - " << profileName << endl;
}


FaubCache::~FaubCache() {
}


void FaubCache::restoreCache(string profileName) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    queue<string> dirQueue;
    string currentDir;
    Pcre regEx("-(\\d{4})(\\d{2})(\\d{2})(?:@(\\d{2}):(\\d{2}):(\\d{2}))*");
    auto refTime = time(NULL);
    Pcre tempRE("\\.tmp\\.\\d+$");

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
                        cerr << "examining " << fullFilename << endl;
                        auto depth = count(fullFilename.begin(), fullFilename.end(), '/') - baseSlashes;
                        if (depth == 3) {
                            // next we make sure the subdir matches our profile name
                            if (fullFilename.find(string("/") + profileName + "-") != string::npos) {
                                // check for in process backups
                                if (tempRE.search(fullFilename))
                                    inProcessFilename = fullFilename;
                                else {
                                    FaubEntry entry(fullFilename);
                                    auto success = entry.loadStats();
                                    DEBUG(D_faub|D_cache) DFMT("loading cache for " << fullFilename << (success ? ": success" : ": failed"));
                                    if (!success || !entry.finishTime || !entry.startDay) {
                                        /* here we have a backup in a directory but no cache file to describe it. all the diskstats
                                        * for that cache file can be recalculated by traversing the backup.  but the finishTime &
                                        * duration are lost. let's use the start time (from the filename) as a ballpark to seed
                                        * the finish time, which will allow the stats output to show an age. */
                                     
                                        if (regEx.search(pathSplit(fullFilename).file) && regEx.matches() > 2) {
                                            struct tm t;

                                            t.tm_year = stoi(regEx.get_match(0)) - 1900;
                                            t.tm_mon  = stoi(regEx.get_match(1)) - 1;
                                            t.tm_mday = stoi(regEx.get_match(2));
                                            t.tm_isdst = -1;

                                            if (regEx.matches() > 5) {
                                                t.tm_hour = stoi(regEx.get_match(3));
                                                t.tm_min = stoi(regEx.get_match(4));
                                                t.tm_sec = stoi(regEx.get_match(5));
                                            }
                                            else {
                                                t.tm_hour = 0;
                                                t.tm_min = 0;
                                                t.tm_sec = 0;
                                            }

                                            if (!entry.finishTime)
                                                entry.finishTime = mktime(&t);

                                            if (!entry.startDay) {
                                                entry.startDay = t.tm_mday;
                                                entry.startMonth = t.tm_mon + 1;
                                                entry.startYear = t.tm_year + 1900;
                                            }

                                            entry.dayAge = floor((refTime - entry.finishTime) / SECS_PER_DAY);
                                            auto pFileTime = localtime(&entry.finishTime);
                                            entry.dow = pFileTime->tm_wday;
                                        }
                                    }

                                    backups.insert(backups.end(), pair<string, FaubEntry>(fullFilename, entry));
                                    continue;
                                }
                            }
                        }

                        if (depth < 3)
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
        DEBUG(D_faub) DFMT("cache set has " << aBackup->first << " with size " << aBackup->second.ds.sizeInBytes << " bytes, " << aBackup->second.duration << " duration");

        if (!aBackup->second.ds.sizeInBytes ||            // if we have no cached data for this backup or
            ((dir.length() && dir == aBackup->first) &&   // (this is a backup we've specifically been asked to recache
            !aBackup->second.updated)) {                  // and it hasn't already been updated on this run of the app)

            bool gotPrev = prevBackup != backups.end();
            if (gotPrev) 
                prevBackup->second.loadInodes();

            set<ino_t> emptySet;
            auto ds = dus(aBackup->first, gotPrev ? prevBackup->second.inodes : emptySet, aBackup->second.inodes);
            DEBUG(D_faub) DFMT("dus(" << aBackup->first << ") returned " << ds.sizeInBytes << " bytes");
            aBackup->second.ds = ds;
            aBackup->second.updated = true;
        }

        prevBackup = aBackup;
    }
}


DiskStats FaubCache::getTotalStats() {
    DiskStats ds;

    for (auto &aBackup: backups) {
        ds += aBackup.second.ds;
    }

    return ds;
}


