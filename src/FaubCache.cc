
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


struct restoreCacheDataType {
    FaubCache *fc;
    queue<string> *q;
    string currentDir;
    int baseSlashes;
    Pcre *tempRE;
};


bool restoreCacheCallback(pdCallbackData &file) {
    restoreCacheDataType *data = (restoreCacheDataType*)file.dataPtr;
    
    if (S_ISDIR(file.statData.st_mode)) {
        /* regardless of the starting (baseDir) directory, we're only interested in subdirs
           exactly 2 levels lower because that's where our backups will live. e.g.
           baseDir = /tmp/backups then we're looking for things like /tmp/backups/2023/01.
           or 3 levels lower if --time is used and hence we get a "day" subdir.
         
           this is a bit different.  instead of letting processDirectory() queue up all the
           subdirs it finds and recursively handle them itself, we limit that with the
           maxDepth = 1 parameter, and only queue up the subdirs we're interested in here
           in the callback function (see the push() call).
         */
        auto depth = count(file.filename.begin(), file.filename.end(), '/') - data->baseSlashes;
        auto dirPs = pathSplit(data->currentDir);
        auto filePs = pathSplit(file.filename);
        bool dirIsDay = (dirPs.file.length() == 2 && isdigit(dirPs.file[0]) && isdigit(dirPs.file[1]));
        bool entIsDay = (filePs.file.length() == 2 && isdigit(filePs.file[0]) && isdigit(filePs.file[1]));
                
        if (depth == 3 || (depth == 4 && dirIsDay)) {
            // next we make sure the subdir matches our profile name
            if (file.filename.find(string("/") + data->fc->coreProfile + "-") != string::npos) {
                
                // check for in process backups
                if (data->tempRE->search(file.filename)) {
                    data->fc->inProcessFilename = file.filename;
                    
                    if (GLOBALS.startupTime - file.statData.st_mtime > 60*60*5) {
                        if (GLOBALS.cli.count(CLI_TEST))
                            cout << YELLOW << " TESTMODE: would have cleaned up abandoned in-process backup at " + file.filename + " (" + timeDiff(mktimeval(file.statData.st_mtime)) + ")" << RESET << endl;
                        else
                            if (rmrf(file.filename))
                                log("warning: cleaned up abandoned in-process backup at " + file.filename + " (" + timeDiff(mktimeval(file.statData.st_mtime)) + ")");
                            else
                                log("error: unable to remove abandoned in-process backup at " + file.filename + " (running as uid " + to_string(geteuid()) + ")");
                    }
                }
                else {
                    if (data->fc->backups.find(file.filename) == data->fc->backups.end()) {
                        data->fc->restoreCache_internal(file.filename);
                        return true;
                    }
                }
            }
        }
        
        if (depth < 3 || (depth == 3 && entIsDay))
            data->q->push(file.filename);
    }
    
    return true;
}


/*
   load or reload all profile name-matching entries from disk.
   don't reload anything that's already in the cache.
 */

void FaubCache::restoreCache(string profileName) {
    queue<string> dirQueue;
    restoreCacheDataType data;
    data.fc = this;
    data.q = &dirQueue;
    Pcre tempRE("\\.tmp\\.\\d+$");
    data.tempRE = &tempRE;
    
    coreProfile = profileName;
    DEBUG(D_faub) DFMT(profileName);
    
    dirQueue.push(baseDir);
    data.baseSlashes = (int)count(baseDir.begin(), baseDir.end(), '/');

    while (dirQueue.size()) {
        data.currentDir = dirQueue.front();
        dirQueue.pop();
        DEBUG(D_faub) DFMT("processing " << data.currentDir);
        processDirectory(ue(data.currentDir), "", false, restoreCacheCallback, &data, 1);
    }

    // all backups are loaded; now see which are missing stats and recache them
    recache("");
}

/*
 recache() runs a dus() on the backups and updates the cache with their current
 disk usage.  which backups are recached can be selected as:
 
 (1) a single specific backup - full backupdir directory specified as targetDir
 
 (2) a single backup after delete - when a backup is deleted (such as by cleanup())
 the time of that deleted backup can be passed in as deletedTime.  recache() will dus()
 the backup with the next sequential time after the deleted one.
 
 (3) as required - the general of catch all case is to loop through all backups of
 the profile and run dus() on any that we don't already have cached stats on.
 */
void FaubCache::recache(string targetDir, time_t deletedTime, bool forceAll) {
    map<string, FaubEntry>::iterator prevBackup = backups.end();
    unsigned int recached = 0;

    if (targetDir.length() && backups.find(targetDir) == backups.end())
        restoreCache_internal(targetDir);
        
    for (auto aBackup = backups.begin(); aBackup != backups.end(); ++aBackup) {
        bool deletedMatch = deletedTime && filename2Mtime(aBackup->first) > deletedTime;

        // we're doing all
        if (forceAll ||
            
            // this is a backup we've specifically been asked to recache
            (targetDir.length() && targetDir == aBackup->first) ||
            
             // if we have no cached data for this backup or its the next sequential backup
             // after the time of a deleted one
            (!targetDir.length() && ((!aBackup->second.ds.sizeInBytes && !aBackup->second.ds.savedInBytes) || deletedMatch))) {
            
            bool gotPrev = prevBackup != backups.end();
            if (gotPrev)
                prevBackup->second.loadInodes();

            ++recached;
            set<ino_t> emptySet;
            auto ds = dus(aBackup->first, gotPrev ? prevBackup->second.inodes : emptySet, aBackup->second.inodes);
            DEBUG(D_any) DFMT("dus(" << aBackup->first << ") returned " << ds.sizeInBytes + ds.savedInBytes << " size bytes, " << ds.sizeInBytes << " used bytes");
            aBackup->second.ds = ds;
            aBackup->second.updated = true;
            aBackup->second.saveStats();
            aBackup->second.saveInodes();
            
            if (forceAll && NOTQUIET)
                cout << BOLDBLUE << aBackup->first << "  " << RESET << "size: " << approximate(ds.getSize() + ds.getSaved()) << ", used: " << approximate(ds.getSize()) << endl;
                
            // the inode list can be long and suck memory.  so let's not let multiple cache entries
            // all keep their inode lists populated at the same time.
            if (gotPrev)
                prevBackup->second.unloadInodes();
            
            // when we're recaching a single backup no need to loop through the rest of them
            if (targetDir.length() || deletedMatch)
                break;
        }

        prevBackup = aBackup;
    }
    
    if (forceAll)
        cout << "caches updated for " << plural(recached, "backup") << "." << endl;
    
    DEBUG(D_faub) DFMT("complete");
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


map<string, FaubEntry, cmpName>::iterator FaubCache::findBackup(string backupDir) {
    set<string> contenders;
    auto backupIt = backups.find(backupDir);

    if (backupIt == backups.end()) {
        Pcre regex(backupDir);
        map<string, FaubEntry, cmpName>::iterator match;

        for (auto it = backups.begin(); it != backups.end(); ++it) {
            if (regex.search(it->first)) {
                contenders.insert(contenders.end(), it->first);
                match = it;
            }
        }

        if (contenders.size() == 1)
            return match;
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
    
    return backups.end();
}


void FaubCache::displayDiffFiles(string backupDir, bool fullPaths) {
    auto backup = findBackup(backupDir);
    
    if (backup != backups.end())
        backup->second.displayDiffFiles(fullPaths);
}


void compareDirs(string dirA, string dirB, size_t threshold, bool percent) {
    map<string, string> subDirs;
    map<string, bool> seenFiles;
    DIR *c_dir;
    struct dirent *c_dirEntry;
    struct stat statDataA;
    struct stat statDataB;
    
    /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-
     walk directory aFile
     *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/

    if ((c_dir = opendir(dirA.c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                continue;
     
            auto aFile = slashConcat(dirA, c_dirEntry->d_name);
            auto bFile = slashConcat(dirB, c_dirEntry->d_name);
            
            if (mylstat(aFile, &statDataA)) {
                SCREENERR("error: unable to stat " << aFile << " - " << strerror(errno));
                exit(1);
            }
            
            if (mylstat(bFile, &statDataB))
                statDataB.st_ino = statDataB.st_size = 0;  // file doesn't exist in the other backup
            
            seenFiles.insert(seenFiles.end(), pair<string, bool>(aFile, false));  // bool value is unused
            
            if (statDataA.st_ino != statDataB.st_ino) {
                auto fileType = getFilesystemEntryType(statDataA.st_mode);
                
                if (S_ISDIR(statDataA.st_mode))
                    subDirs.insert(subDirs.end(), pair<string, string>(aFile, bFile));
                else {
                    auto sizeChange = statDataA.st_size - statDataB.st_size;

                    if (percent) {
                        long p = 0;
                        
                        if (statDataA.st_size)
                            p = floor((double)abs(sizeChange) / (double)statDataA.st_size * 100.0);
                        else
                            if (sizeChange)
                                p = threshold;
                        
                        if (percent >= threshold)
                            if (!GLOBALS.cli.count(CLI_COMPFOCUS) || (GLOBALS.cli.count(CLI_COMPFOCUS) && fileType != 'l' && fileType != 'd'))
                                cout << BOLDRED << blockp(to_string(p) + "%", 4) << " [" << fileType << "]  " << RESET << aFile << endl;
                    }
                    else
                        if (abs(sizeChange) >= threshold)
                            if (!GLOBALS.cli.count(CLI_COMPFOCUS) || (GLOBALS.cli.count(CLI_COMPFOCUS) && fileType != 'l' && fileType != 'd'))
                                cout << BOLDRED << blockp((sizeChange >= 0 ? "+" : "-") + approximate(abs(sizeChange)), 5) << RESET << " [" << fileType << "]  " << RESET << aFile << endl;
                }
            }
        }
        closedir(c_dir);
    }

    
    /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-
     walk directory bFile
     *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/

    if ((c_dir = opendir(dirB.c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                continue;
     
            auto aFile = slashConcat(dirA, c_dirEntry->d_name);
            auto bFile = slashConcat(dirB, c_dirEntry->d_name);
            
            if (seenFiles.find(aFile) != seenFiles.end())
                continue;

            if (mylstat(bFile, &statDataB)) {
                SCREENERR("error: unable to stat " << bFile << " - " << strerror(errno));
                exit(1);
            }

            auto fileType = getFilesystemEntryType(statDataB.st_mode);

            if (percent || statDataB.st_size >= threshold)
                if (!GLOBALS.cli.count(CLI_COMPFOCUS) || (GLOBALS.cli.count(CLI_COMPFOCUS) && fileType != 'l' && fileType != 'd'))
                    cout << BOLDRED << blockp("+" + approximate(statDataB.st_size), 5) << RESET << " [" << fileType << "]  " << RESET << bFile << endl;
        }
        closedir(c_dir);
    }
    
    
    /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-
     walk subdirs
     *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/

    for (auto &dirs: subDirs)
        compareDirs(dirs.first, dirs.second, threshold, percent);
}


void FaubCache::compare(string backupA, string backupB, string givenThreshold) {
    Pcre threshRE("(\\d+)\\s*(%)*");
    bool percent = false;
    size_t threshold = 0;
    
    // no threshold
    if (!givenThreshold.length()) {
        // above defaults are perfect
    }
    else
        // percentage threshold
        if (threshRE.search(givenThreshold) && threshRE.matches() > 1) {
            threshold = stoll(threshRE.get_match(0));
            percent = true;
        }
        else {
            try {
                // parse number and suffix
                threshold = approx2bytes(givenThreshold);
            }
            catch (...) {
                SCREENERR("error: invalid threshold; use a raw number of bytes, a number with a suffix (e.g. 5K) or a percentage (e.g. 15%)");
                exit(1);
            }
        }
    
    auto b1 = findBackup(backupA);
    auto b2 = findBackup(backupB);
    
    if (b1 != backups.end() && b2 != backups.end()) {
        auto b1Base = b1->second.getDir();
        auto b2Base = b2->second.getDir();
        
        cout << BOLDYELLOW << "[" << BOLDBLUE << b1Base << BOLDYELLOW << "]\n";
        cout << BOLDYELLOW << "[" << BOLDBLUE << b2Base << BOLDYELLOW << "]" << RESET << endl;
        
        compareDirs(b1Base, b2Base, threshold, percent);
    }
}


bool fcCleanupCallback(pdCallbackData &file) {
    ifstream cacheFile;
    string backupDir, fullId, profileName;
    string cacheFilename;
    struct stat statData;
    FaubCache *fc = (FaubCache*)file.dataPtr;
    
    cacheFile.open(file.filename);

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
        if (mystat(backupDir, &statData)) {
            auto targetMtime = filename2Mtime(backupDir);

            if (profileName.length()) {
                Pcre yearRE("^20\\d{2}$");
                auto parentDir = backupDir;
                auto ps = pathSplit(parentDir);

                while (parentDir.length() > 1 && parentDir.find("/") != string::npos && !yearRE.search(ps.file)) {
                    parentDir = ps.dir;
                    ps = pathSplit(parentDir);
                }

                auto bdir = pathSplit(parentDir).dir;
                if (profileName == fc->coreProfile && bdir == fc->baseDir) {
                    DEBUG(D_any) DFMT(backupDir << " no longer exists; will recalculate usage of subsequent backup");
                    unlink(cacheFilename.c_str());
                    cacheFilename.replace(cacheFilename.find(SUFFIX_FAUBSTATS), string(SUFFIX_FAUBSTATS).length(), SUFFIX_FAUBINODES);
                    unlink(cacheFilename.c_str());
                    cacheFilename.replace(cacheFilename.find(SUFFIX_FAUBINODES), string(SUFFIX_FAUBINODES).length(), SUFFIX_FAUBDIFF);
                    unlink(cacheFilename.c_str());
  
                    // re-dus the next backup
                    fc->recache("", targetMtime);
                }
            }
        }
    }
    
    return true;
}


/* cleanup()
 Walk through this cache's files looking for cache's that reference backups
 that no longer exist (have been removed).  When one is found, we delete the
 cache files. But we also look for the next backup matching the same dir +
 profile name as the deleted one and recalculate (dus) its disk usage because
 that will have changed.  If multiple backups sequential backups are found
 missing from the same dir + profile we re-dus the next one that's found.
 */
void FaubCache::cleanup() {
    FaubCache *fc = this;
    
    DEBUG(D_faub) DFMT("starting");
    processDirectory(GLOBALS.cacheDir, SUFFIX_FAUBSTATS, false, fcCleanupCallback, fc);
    DEBUG(D_faub) DFMT("complete");
}


void FaubCache::renameBaseDirTo(string newDir) {
    for (auto &backup: backups)
        backup.second.renameDirectoryTo(newDir, baseDir);
    
    baseDir = newDir;
}
