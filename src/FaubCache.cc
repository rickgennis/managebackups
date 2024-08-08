
#include <fstream>
#include <algorithm>
#include <queue>
#include <unordered_set>
#include <dirent.h>
#include <unistd.h>
#include "globals.h"
#include "FaubCache.h"
#include "BackupConfig.h"
#include "util_generic.h"
#include "debug.h"
#include "FastCache.h"
#include "tagging.h"


FaubCache::FaubCache(string path, string profileName, string aUuid) {
    baseDir = path;
    uuid = aUuid;
    
    if (path.length())
        restoreCache(profileName);
}


void FaubCache::restoreCache(string path, string profileName, string aUuid) {
    baseDir = path;
    uuid = aUuid;
    
    if (path.length())
        restoreCache(profileName);
}


void FaubCache::restoreCache_internal(string backupDir) {
    Pcre regEx(DATE_REGEX);
    FaubEntry entry(backupDir, coreProfile, uuid);
    auto success = entry.loadStats();
    DEBUG(D_cache) DFMT("loading cache for " << backupDir << (success ? ": success" : ": failed"));
    
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
    Pcre *tempRE;
};


bool restoreCacheCallback(pdCallbackData &file) {
    restoreCacheDataType *data = (restoreCacheDataType*)file.dataPtr;
    
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
    else
        // otherwise see if this backup still needs to be loaded
        if (data->fc->backups.find(file.filename) == data->fc->backups.end())
            data->fc->restoreCache_internal(file.filename);
    
    return true;
}


/*
 load or reload all profile name-matching entries from disk.
 don't reload anything that's already in the cache.
 */

void FaubCache::restoreCache(string profileName) {
    restoreCacheDataType data;
    timer restoreTimer;
    Pcre tempRE("\\.tmp\\.\\d+$");
    data.tempRE = &tempRE;
    data.fc = this;

    restoreTimer.start();
    
    coreProfile = profileName;
    DEBUG(D_faub) DFMT(profileName);
    
    processDirectoryBackups(ue(baseDir), "/" + coreProfile + "-", true, restoreCacheCallback, &data, FAUB_ONLY);

    restoreTimer.stop();
    
    DEBUG(D_faub) DFMT("complete - " << restoreTimer.elapsed(5));

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
 
 (3) as required - the general catch all case is to loop through all backups of
 the profile and run dus() on any that we don't already have cached stats on.
 */
void FaubCache::recache(string targetDir, time_t deletedTime, bool forceAll) {
    myMapIT prevBackup = backups.end();
    unsigned int recached = 0;
    bool nextOneToo = false;
    statusMessage message("recalculating disk usage... ");
    timer recacheTimer;
    
    recacheTimer.start();
    
    if (targetDir.length() && backups.find(targetDir) == backups.end())
        restoreCache_internal(targetDir);
    
    for (auto aBackup = backups.begin(); aBackup != backups.end(); ++aBackup) {
        DEBUG(D_recalc) DFMT("examining " << aBackup->first);
        bool deletedMatch = deletedTime && filename2Mtime(aBackup->first) > deletedTime;
        
        // we're doing all
        if (forceAll ||
            
            // this is a backup we've specifically been asked to recache
            (targetDir.length() && targetDir == aBackup->first) ||
            
            // if we have no cached data for this backup or its the next sequential backup
            // after the time of a deleted one
            (!targetDir.length() && ((!aBackup->second.ds.usedInBytes && !aBackup->second.ds.savedInBytes) || deletedMatch)) ||
            
            // in the case of walking through all the backups and doing just the ones that are missing stats
            // (the immediately above condition of this "if") then we also have to re-do the next backup too
            // because a change in that one could effect the next one
            nextOneToo) {
            
            bool gotPrev = prevBackup != backups.end();
            string prevDesc;
            if (gotPrev) {
                prevBackup->second.loadInodes();
                prevDesc = pathSplit(prevBackup->first).file;
            }
            
            if (!recached)
                NOTQUIET && ANIMATE && message.show();
            
            ++recached;
            set<ino_t> emptySet;
            aBackup->second.unloadInodes();
            auto ds = dus(aBackup->first, gotPrev ? prevBackup->second.inodes : emptySet, aBackup->second.inodes);
            
            DEBUG(D_any) DFMT("dus " << aBackup->first << ": " <<
                              ds.usedInBytes + ds.savedInBytes << " total, " <<
                              ds.usedInBytes << " used (" <<
                              to_string(forceAll) + string(",") +
                              to_string(targetDir.length() && targetDir == aBackup->first) + "," +
                              to_string(!targetDir.length() && ((!aBackup->second.ds.usedInBytes && !aBackup->second.ds.savedInBytes) || deletedMatch)) + "," +
                              to_string(nextOneToo) + ")" +
                              (gotPrev ? "; " + prevDesc : "; -"));

            string reason = targetDir.length() && targetDir == aBackup->first ? "specific" :
                (!targetDir.length() && (!aBackup->second.ds.usedInBytes && !aBackup->second.ds.savedInBytes)) ? "missing" :
                (!targetDir.length() && deletedMatch) ? "post-delete" :
                nextOneToo ? "next" : "?";
            
            log("stat calc " + aBackup->first + " (" + reason + "): " +
                              to_string(ds.usedInBytes + ds.savedInBytes) + " total, " +
                              to_string(ds.usedInBytes) + " used" +
                              (gotPrev ? "; vs " + prevDesc : "; -"));
            
            aBackup->second.ds = ds;
            aBackup->second.updated = true;
            aBackup->second.dirs = ds.dirs;
            aBackup->second.slinks = ds.symLinks;
            aBackup->second.modifiedFiles = ds.mods;
            aBackup->second.saveStats();
            aBackup->second.saveInodes();
            
            if (forceAll && NOTQUIET) {
                ANIMATE && message.remove();
                
                cout << BOLDBLUE << aBackup->first << "  " << RESET << "size: " << approximate(ds.getSize() + ds.getSaved()) << ", used: " << approximate(ds.getSize()) <<
                ", dirs: " << approximate(ds.dirs) << ", links: " << approximate(ds.symLinks) << ", mods: " << approximate(ds.mods) << endl;
            }
            
            // the inode list can be long and suck memory.  so let's not let multiple cache entries
            // all keep their inode lists populated at the same time.
            if (gotPrev)
                prevBackup->second.unloadInodes();
            
            // when we're recaching a single backup no need to loop through the rest of them
            if (targetDir.length() || deletedMatch)
                break;
            
            // the above "break" knocks out targeted backups and redoing the one right after a delete.
            // what remains are that we're doing all of them (forceAll) or walking through all of them
            // and just doing the ones that are missing stats.  if it's the latter and we're just updating
            // missing stats, we need to do one more after each missing stats one.
            if (!forceAll)
                nextOneToo = !nextOneToo;
        }
        
        prevBackup = aBackup;
    }
    
    if (recached)
        NOTQUIET && ANIMATE && !forceAll && message.remove();
    
    if (forceAll && NOTQUIET)
        cout << "caches updated for " << plural(recached, "backup") << "." << endl;
    
    recacheTimer.stop();
    DEBUG(D_faub) DFMT("complete - " << recacheTimer.elapsed(5));
    
    if (recached) {
        FastCache fc;
        fc.invalidate();
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


myMapIT FaubCache::findBackup(string backupDir, myMapIT backupIT) {
    set<string> contenders;
    
    if (backupIT != backups.end()) {
        if (backupIT != backups.begin())
            return --backupIT;
        
        return backups.end();
    }
    else
        if (!backupDir.length()) {
            if (backups.rbegin() != backups.rend())
                return (++backups.rbegin()).base();
            
            return backups.end();
        }
    
    auto backupIt = backups.find(backupDir);
    
    if (backupIt == backups.end()) {
        Pcre regex(backupDir);
        myMapIT match;
        
        for (auto it = backups.begin(); it != backups.end(); ++it) {
            if (regex.search(it->first)) {
                contenders.insert(contenders.end(), it->first);
                match = it;
            }
        }
        
        if (contenders.size() == 1)
            return match;
        else {
            if (contenders.size() > 1) {
                cout << "error: multiple backups match -" << endl;
                for (auto &bkup: contenders)
                    cout << "\t" << bkup << endl;
                cout << "be more specific." << endl;
            }
            else
                cerr << "unable to find " << backupDir << " in cache." << endl;
            exit(1);
        }
    }
    
    return backupIt;
}


bool FaubCache::displayDiffFiles(string backupDir) {
    auto backup = findBackup(backupDir, backups.end());
    
    if (backup != backups.end())
        return backup->second.displayDiffFiles();
    
    return false;
}


void FaubCache::tagBackup(string tagname, string backup) {
    auto b = findBackup(backup, backups.end());
    auto backupname = b->second.getDir();
    
    GLOBALS.tags.tagBackup(tagname, backupname);
    cout << "\t• tagged " << backupname << " as " << tagname << endl;
}


string FaubCache::holdBackup(string hold, string backup) {
    auto b = findBackup(backup, backups.end());
    auto backupname = b->second.getDir();

    // 1 is for permanent hold
    b->second.holdDate = (hold == "::") ? 1 : userInput2timet(hold);
    b->second.saveStats();
    
    if (!b->second.holdDate)
        return("\t hold removed for " + backupname + "\n");

    if (b->second.holdDate == 1)
        return("\t• permanent hold set on " + backupname + "\n");
    
    return("\t• hold set on " + backupname + " until " + timeString(b->second.holdDate) + "\n");
}


struct compareDirsDataType {
    size_t filesChanged;
    size_t bytesChanged;
    size_t bytesUsed;
    size_t filesA;
    size_t filesB;
    unordered_set<string> seenFiles;
    string baseDirA;
    string baseDirB;
    size_t threshold;
    bool percent;
    bool filter;
};


bool compareDirsCallbackA(pdCallbackData &file) {
    compareDirsDataType *data = (compareDirsDataType*)file.dataPtr;
    struct stat statDataB;
    
    // file.filename is fileA
    // fileB is the same filename but with the dirB baseDir instead of the dirA one
    string fileB = file.filename;
    fileB.replace(0, data->baseDirA.length(), data->baseDirB);
    
    ++data->filesA;
    
    if (mylstat(fileB, &statDataB)) // file doesn't exist in dirB
        statDataB.st_ino = statDataB.st_size = 0;
    else
        ++data->filesB;

    data->seenFiles.insert(file.filename);
    
    if (file.statData.st_ino != statDataB.st_ino) {
        auto fileType = getFilesystemEntryType(file.statData.st_mode);
        auto sizeChange = file.statData.st_size - statDataB.st_size;
        
        if (data->percent) {
            long p = 0;
            
            if (file.statData.st_size)
                p = floor((double)abs(sizeChange) / (double)file.statData.st_size * 100.0);
            else
                if (sizeChange)
                    p = data->threshold;
            
            if (data->percent >= data->threshold)
                if (!data->filter || (data->filter && fileType != 'l' && fileType != 'd')) {
                    cout << BOLDRED
                    << blockp(to_string(p) + "%", 4) << RESET
                    << " " << blockp(approximate(file.statData.st_size), 4)
                    << " [" << fileType << "]  " << file.filename << endl;
                    ++data->filesChanged;
                    data->bytesChanged += abs(sizeChange);
                    data->bytesUsed += file.statData.st_size;
                }
        }
        else
            if (abs(sizeChange) >= data->threshold)
                if (!data->filter || (data->filter && fileType != 'l' && fileType != 'd')) {
                    cout << BOLDRED
                    << blockp((sizeChange >= 0 ? "+" : "-") + approximate(abs(sizeChange)), 5)
                    << RESET << " " << blockp(approximate(file.statData.st_size), 4)
                    << " [" << fileType << "]  "
                    << file.filename << endl;
                    ++data->filesChanged;
                    data->bytesChanged += abs(sizeChange);
                    data->bytesUsed += file.statData.st_size;
                }
    }
    
    return true;
}


bool compareDirsCallbackB(pdCallbackData &file) {
    compareDirsDataType *data = (compareDirsDataType*)file.dataPtr;
    
    // file.filename is fileB
    // fileA is the same filename but with the dirA baseDir instead of the dirB one
    string fileA = file.filename;
    fileA.replace(0, data->baseDirB.length(), data->baseDirA);
    
    if (data->seenFiles.find(fileA) == data->seenFiles.end()) {
        auto fileType = getFilesystemEntryType(file.statData.st_mode);
        ++data->filesB;

        if (data->percent || file.statData.st_size >= data->threshold)
            if (!data->filter || (data->filter && fileType != 'l' && fileType != 'd')) {
                cout << BOLDRED << blockp("+" + approximate(file.statData.st_size), 5) << RESET << " [" << fileType << "]  " << RESET << file.filename << endl;
                ++data->filesChanged;
                data->bytesChanged += file.statData.st_size;
                data->bytesUsed += file.statData.st_size;
            }
    }
    
    return true;
}


tuple<size_t, size_t, size_t, size_t, size_t> compareDirs(string dirA, string dirB, size_t threshold, bool percent) {
    compareDirsDataType data;
    data.filter = !GLOBALS.cli.count(CLI_COMPAREFILTER);
    data.percent = percent;
    data.threshold = threshold;
    data.filesChanged = data.bytesChanged = data.bytesUsed = data.filesA = data.filesB = 0;
    data.baseDirA = dirA;
    data.baseDirB = dirB;
    
    // walk through the first of the backups to compare (dirA).
    // note all discovered files.  if the same file doesn't exist in the other
    // backups (dirB) with the same inode, then output its diff details.
    processDirectory(dirA, "", false, false, compareDirsCallbackA, &data);

    // walk through the second of the backups to compare (dirB).  if the
    // filename is on the dirA list of discoverd files, skip it.  otherwise it's
    // a file that only exists in the second backup (i.e. not discoverable in the
    // dirA scan).  so output its diff details here.
    processDirectory(dirB, "", false, false, compareDirsCallbackB, &data);
    
    return {data.filesChanged, data.bytesChanged, data.bytesUsed, data.filesA, data.filesB};
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
    
    auto b1 = findBackup(backupA, backups.end());
    auto b2 = findBackup(backupB, backupB.length() ? backups.end() : b1);
    
    auto b1Base = b1->second.getDir();
    auto b2Base = b2->second.getDir();
    
    cout << BOLDYELLOW << "[" << BOLDBLUE << b1Base << BOLDYELLOW << "]\n";
    cout << "[" << BOLDBLUE << b2Base << BOLDYELLOW << "]" << endl;
    auto [changes, changeSize, changeUsed, filesA, filesB] = compareDirs(b1Base, b2Base, threshold, percent);
    cout << BOLDYELLOW << approximate(changeSize) << BOLDBLUE << " in changes using " << BOLDYELLOW << approximate(changeUsed) << BOLDBLUE << " on disk across " << BOLDYELLOW << plural(changes, string(BOLDBLUE) + "file") << " (A entries: " << approximate(filesA) << ", B entries: " << approximate(filesB) << ")" << RESET << endl;
}


bool fcCleanupCallback(pdCallbackData &file) {
    ifstream cacheFile;
    string backupDir, fullId, profileName;
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
        if (!exists(backupDir)) {
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
                    FaubEntry entry(backupDir, fc->coreProfile, fc->uuid);
                    entry.removeEntry();
                    log(backupDir + " has vanished, updating cache");

                    // re-dus the next backup
                    fc->recache("", targetMtime);
                }
            }
        }
    }
    
    return true;
}


/* cleanup()
 Walk through this cache's files looking for caches that reference backups
 that no longer exist (have been removed).  When one is found, we delete the
 cache files. But we also look for the next backup matching the same dir +
 profile name as the deleted one and recalculate (dus) its disk usage because
 that will have changed.  If multiple sequential backups are found
 missing from the same dir + profile we re-dus the next one that's found.
 */
void FaubCache::cleanup() {
    FaubCache *fc = this;
    
    processDirectory(slashConcat(GLOBALS.cacheDir, uuid), string(SUFFIX_FAUBSTATS) + "$", false, false, fcCleanupCallback, fc);
}


void FaubCache::renameBaseDirTo(string newDir) {
    for (auto &backup: backups)
        backup.second.renameDirectoryTo(newDir, baseDir);
    
    baseDir = newDir;
}
