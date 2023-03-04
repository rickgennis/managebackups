
#include <fstream>
#include <algorithm>
#include <queue>
#include <dirent.h>
#include <unistd.h>
#include "globals.h"
#include "FaubCache.h"
#include "BackupConfig.h"
#include "util_generic.h"
#include "debug.h"

#include "FaubCache.h"


FaubCache::FaubCache(string path, string profileName) {
    baseDir = path;
    
    if (path.length())
        restoreCache(profileName);
}


void FaubCache::restoreCache(string path, string profileName) {
    baseDir = path;
    
    if (path.length())
        restoreCache(profileName);
}


void FaubCache::restoreCache_internal(string backupDir) {
    Pcre regEx(DATE_REGEX);
    FaubEntry entry(backupDir, coreProfile);
    auto success = entry.loadStats();
    DEBUG(D_faub|D_cache) DFMT("loading cache for " << backupDir << (success ? ": success" : ": failed"));
    
    if (!success || !entry.finishTime || !entry.startDay) {
        /* here we have a backup in a directory but no cache file to describe it. all the diskstats
         * for that cache file can be recalculated by traversing the backup.  but the finishTime &
         * duration are lost. let's use the start time (from the filename) as a ballpark to seed
         * the finish time, which will allow the stats output to show an age. */
        
        if (regEx.search(pathSplit(backupDir).file) && regEx.matches() > 2) {
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
            
            entry.mtimeDayAge = floor((time(NULL) - entry.finishTime) / SECS_PER_DAY);
            auto pFileTime = localtime(&entry.finishTime);
            entry.dow = pFileTime->tm_wday;
        }
    }
    
    backups.insert(backups.end(), pair<string, FaubEntry>(backupDir, entry));
}


/*
   load or reload all profile name-matching entries from disk.
   don't reload anything that's already in the cache.
 */
void FaubCache::restoreCache(string profileName) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    queue<string> dirQueue;
    string currentDir;
    Pcre tempRE("\\.tmp\\.\\d+$");

    coreProfile = profileName;
    
    dirQueue.push(baseDir);
    auto baseSlashes = count(baseDir.begin(), baseDir.end(), '/');

    while (dirQueue.size()) {
        currentDir = dirQueue.front();
        dirQueue.pop();

        if ((c_dir = opendir(ue(currentDir).c_str())) != NULL) {
            while ((c_dirEntry = readdir(c_dir)) != NULL) {
                
                if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                    continue;
                
                struct stat statData;
                string fullFilename = slashConcat(currentDir, c_dirEntry->d_name);
                
                ++GLOBALS.statsCount;
                if (!stat(fullFilename.c_str(), &statData)) {
                    
                    if (S_ISDIR(statData.st_mode)) {
                        // regardless of the starting (baseDir) directory, we're only interested in subdirs
                        // exactly 2 levels lower because that's where our backups will live. e.g.
                        // baseDir = /tmp/backups then we're looking for things like /tmp/backups/2023/01.
                        // or 3 levels lower if --time is used and hence we get a "day" subdir.
                        auto depth = count(fullFilename.begin(), fullFilename.end(), '/') - baseSlashes;
                        auto ps = pathSplit(currentDir);
                        bool dirIsDay = (ps.file.length() == 2 && isdigit(ps.file[0]) && isdigit(ps.file[1]));
                        bool entIsDay = (string(c_dirEntry->d_name).length() == 2 && isdigit(c_dirEntry->d_name[0]) && isdigit(c_dirEntry->d_name[1]));
                        
                        if (depth == 3 || (depth == 4 && dirIsDay)) {
                            // next we make sure the subdir matches our profile name
                            if (fullFilename.find(string("/") + profileName + "-") != string::npos) {
                                // check for in process backups
                                if (tempRE.search(fullFilename)) {
                                    inProcessFilename = fullFilename;
                                    
                                    if (GLOBALS.startupTime - statData.st_mtime > 60*60*5) {
                                        if (GLOBALS.cli.count(CLI_TEST))
                                            cout << YELLOW << " TESTMODE: would have cleaned up abandoned in-process backup at " + fullFilename + " (" + timeDiff(mktimeval(statData.st_mtime)) + ")" << RESET << endl;
                                        else
                                            if (rmrf(fullFilename))
                                                log("warning: cleaned up abandoned in-process backup at " + fullFilename + " (" + timeDiff(mktimeval(statData.st_mtime)) + ")");
                                            else
                                                log("error: unable to remove abandoned in-process backup at " + fullFilename + " (running as uid " + to_string(geteuid()) + ")");
                                    }
                                }
                                else {
                                    if (backups.find(fullFilename) == backups.end()) {
                                        restoreCache_internal(fullFilename);
                                        continue;
                                    }
                                }
                            }
                        }
                        
                        if (depth < 3 || (depth == 3 && entIsDay))
                            dirQueue.push(fullFilename);
                    }
                }
            }
            closedir(c_dir);
        }
    }

    // all backups are loaded; now see which are missing stats and recache them
    recache("");
}


void FaubCache::recache(string targetDir, time_t deletedTime) {
    map<string, FaubEntry>::iterator prevBackup = backups.end();

    if (targetDir.length() && backups.find(targetDir) == backups.end())
        restoreCache_internal(targetDir);
        
    for (auto aBackup = backups.begin(); aBackup != backups.end(); ++aBackup) {
        bool deletedMatch = deletedTime && filename2Mtime(aBackup->first) > deletedTime;

        // this is a backup we've specifically been asked to recache
        if ((targetDir.length() && targetDir == aBackup->first) ||
            (!targetDir.length() &&
             
             // if we have no cached data for this backup or its the next sequential backup
             // after the time of a deleted one
             ((!aBackup->second.ds.sizeInBytes && !aBackup->second.ds.savedInBytes) || deletedMatch))) {
            
            if (deletedMatch)
                deletedTime = 0;  // only want to match the *first* backup after the delete time
            
            bool gotPrev = prevBackup != backups.end();
            if (gotPrev)
                prevBackup->second.loadInodes();
            
            set<ino_t> emptySet;
            auto ds = dus(aBackup->first, gotPrev ? prevBackup->second.inodes : emptySet, aBackup->second.inodes);
            DEBUG(D_faub) DFMT("dus(" << aBackup->first << ") returned " << ds.sizeInBytes << " used bytes, " << ds.savedInBytes << " saved bytes");
            aBackup->second.ds = ds;
            aBackup->second.updated = true;
            
            // the inode list can be long and suck memory.  so let's not let multiple cache entries
            // all keep their inode lists populated at the same time.
            if (gotPrev)
                prevBackup->second.unloadInodes();
            
            // when we're recaching a single backup no need to loop through the rest of them
            if (targetDir.length())
                break;
        }

        prevBackup = aBackup;
    }
}


DiskStats FaubCache::getTotalStats() {
    DiskStats ds;

    for (auto &aBackup: backups)
        ds += aBackup.second.ds;

    return ds;
}


void FaubCache::updateDiffFiles(string backupDir, set<string> files) {
    auto backupIt = backups.find(backupDir);
    if (backupIt != backups.end())
        backupIt->second.updateDiffFiles(files);
    else
        cerr << "unable to find " << backupDir << " in cache." << endl;
}


void FaubCache::displayDiffFiles(string backupDir, bool fullPaths) {
    set<string> contenders;
    auto backupIt = backups.find(backupDir);

    if (backupIt != backups.end())
        backupIt->second.displayDiffFiles();
    else {
        Pcre regex(backupDir);
        map<string, FaubEntry, cmpName>::iterator match;

        for (auto it = backups.begin(); it != backups.end(); ++it) {
            if (regex.search(it->first)) {
                contenders.insert(contenders.end(), it->first);
                match = it;
            }
        }

        if (contenders.size() == 1)
            match->second.displayDiffFiles(fullPaths);
        else
            if (contenders.size() > 1) {
                cout << "error: multiple backups match -" << endl;
                for (auto &bkup: contenders)
                    cout << "\t" << bkup << endl;
                cout << "be more specific." << endl;
            }
            else
                cerr << "unable to find " << backupDir << " in cache." << endl;
    }
}


/* cleanup() ignores the backupDir and profile to work across all faub caches
 Walk through all faub cache files looking for cache's that reference backups
 that no longer exist (have been removed).  When one is found, we delete the
 cache files. But we also look for the next backup matching the same dir +
 profile name as the deleted one and recalculate (dus) its disk usage because
 that will have changed.  If multiple backups sequential backups are found
 missing from the same dir + profile we re-dus the next one that's found.
 */
void FaubCache::cleanup() {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    ifstream cacheFile;
    string backupDir, fullId, profileName;
    string cacheFilename;
    struct stat statData;

    /* this should probably stop ignoring directory and profile to instead
       search just for cache files on disk that match those.  then if the
       backup is gone, still delete those cache files but you'll have the
       cache ordered in memory to figure out how to dus the next sequential
       one. */
    
    if ((c_dir = opendir(ue(GLOBALS.cacheDir).c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {

            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, "..") ||
                    strstr(c_dirEntry->d_name, SUFFIX_FAUBSTATS) == NULL)
                continue;

            cacheFilename = slashConcat(GLOBALS.cacheDir, c_dirEntry->d_name);
            cacheFile.open(cacheFilename);

            if (cacheFile.is_open()) {
                cacheFile >> fullId;
                cacheFile.close();
                
                backupDir = fullId;
                
                auto pos = fullId.find(";;");
                if (pos != string::npos) {
                    profileName = fullId.substr(pos + 2, string::npos);
                    backupDir.erase(backupDir.find(";;"));  // erase the profile name portion
                }
                
                // if the backup directory referenced in the cache file no longer exists
                // delete the cache file
                if (stat(backupDir.c_str(), &statData)) {
                    auto targetMtime = filename2Mtime(backupDir);
  
                    if (profileName.length()) {
                        Pcre yearRE("^20\\d{2}$");
                        auto parentDir = backupDir;
                        auto ps = pathSplit(parentDir);

                        while (parentDir.find("/") != string::npos && !yearRE.search(ps.file)) {
                            parentDir = ps.dir;
                            ps = pathSplit(parentDir);
                        }
                        
                        auto bdir = pathSplit(parentDir).dir;
                        if (profileName == coreProfile && bdir == baseDir) {
                            DEBUG(D_any) DFMT(parentDir << " no longer exists; will recalculate usage of subsequent backup");
                            unlink(cacheFilename.c_str());
                            cacheFilename.replace(cacheFilename.find(SUFFIX_FAUBSTATS), string(SUFFIX_FAUBSTATS).length(), SUFFIX_FAUBINODES);
                            unlink(cacheFilename.c_str());
                            cacheFilename.replace(cacheFilename.find(SUFFIX_FAUBINODES), string(SUFFIX_FAUBINODES).length(), SUFFIX_FAUBDIFF);
                            unlink(cacheFilename.c_str());
          
                            recache("", targetMtime);
                        }
                    }
                }
            }
        }        
        closedir(c_dir);
    }
}
