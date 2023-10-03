#include <dirent.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <algorithm>
#include <unistd.h>
#include <utime.h>

#include "FaubCache.h"
#include "faub.h"
#include "ipc.h"
#include "notify.h"
#include "debug.h"
#include "globals.h"
#include "exception.h"


extern void cleanupAndExitOnError();

struct mostRecentBDataType {
    time_t sinceTime;
    time_t recentTime;
    string recentName;
    Pcre *dateRE;
};


bool mostRecentBCallback(pdCallbackData &file) {
    mostRecentBDataType *data = (mostRecentBDataType*)file.dataPtr;
    
    DEBUG(D_faub) DFMT("considering " << file.filename << " with mtime " << file.statData.st_mtime);
    
    if (file.statData.st_mtime > data->recentTime && ((file.statData.st_mtime < data->sinceTime || !data->sinceTime))) {
        data->recentTime = file.statData.st_mtime;
        data->recentName = file.filename;
        
        DEBUG(D_faub) DFMT("new most recent backup (" << file.filename << ")");
    }
    
    return true;
}


string mostRecentBackupDirSince(string backupDir, string sinceDir, string profileName) {
    Pcre dateRE(DATE_REGEX);
    mostRecentBDataType data;
    data.dateRE = &dateRE;
    data.recentTime = 0;

    struct stat statData;
    if (!mystat(sinceDir, &statData))
        data.sinceTime = statData.st_mtime;
    else
        data.sinceTime = time(NULL);
    
    processDirectoryBackups(backupDir, "/" + profileName + "-", true, mostRecentBCallback, &data, FAUB_ONLY);
    
    return data.recentName;
}


string newBackupDir(BackupConfig& config) {
    time_t rawTime;
    struct tm *timeFields;
    time(&rawTime);
    timeFields = localtime(&rawTime);
    
    bool incTime = str2bool(config.settings[sIncTime].value);
    string setDir = config.settings[sDirectory].value;
    
    char buffer[100];
    strftime(buffer, sizeof(buffer), incTime ? "%Y/%m/%d" : "%Y/%m", timeFields);
    string subDir = buffer;

    strftime(buffer, sizeof(buffer), incTime ? "-%Y-%m-%d@%T" : "-%Y-%m-%d", timeFields);
    string filename = buffer;

    string fullPath = slashConcat(config.settings[sDirectory].value, subDir, safeFilename(config.settings[sTitle].value) + filename);

    return fullPath;
}


void fs_startServer(BackupConfig& config) {
    PipeExec faub(config.settings[sFaub].value, 60);

    if (GLOBALS.cli.count(CLI_NOBACKUP))
        return;

    string newDir = newBackupDir(config);
    string prevDir = mostRecentBackupDirSince(config.settings[sDirectory].value, newDir, config.settings[sTitle].value);

    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW << config.ifTitle() << " TESTMODE: would have begun backup by executing \"" << config.settings[sFaub].value << "\"" << endl;
        cout << "saving to " << newDir << endl;
        cout << "comparing to previous " << prevDir << RESET << endl;
        return;
    }

    DEBUG(D_netproto) DFMT("executing: \"" << config.settings[sFaub].value << "\"");
    faub.execute(GLOBALS.cli.count(CLI_LEAVEOUTPUT) ? config.settings[sTitle].value : "", false, false, false, true);
    fs_serverProcessing(faub, config, prevDir, newDir);
}


void fs_serverProcessing(PipeExec& client, BackupConfig& config, string prevDir, string currentDir) {
    size_t maxLinksReached = 0;
    string remoteFilename;
    string localPrevFilename;
    string localCurFilename;
    string originalCurrentDir = currentDir;
    struct stat statData;
    size_t fileTotal = 0;
    size_t filesModified = 0;
    size_t filesHardLinked = 0;
    size_t filesSymLinked = 0;
    size_t receivedSymLinks = 0;
    size_t unmodDirs = 0;
    size_t linkErrors = 0;
    string tempExtension = ".tmp." + to_string(GLOBALS.pid);
    bool abortBackupAtEnd = false;

    // note start time
    timer backupTime;
    timer fsTime;
    backupTime.start();

    /*
     FAUB PROTOCOL - 4 PHASES
     
     Note: The term filesystem is used here very loosely to mean a directory structure the client
     wants to backup (--path option).  It doesn't really need to coorelate to a filesystem.
     
     1. Client (server being backed up) sends a list comprised of a full path and its mtime for
        all directory entries (files, directories, symlinks) to be backed up.
     2. Server compares each full path name and mtime to the previous backup of this
        filesystem (if any) and makes a new list of the ones that have changed.  Those changes
        are sent back to the client as requests.
     3. Client sends the full detail (uid, gid, mode, mtime, size and the full content of the file)
        for each requested item, which the server writes to disk as newly backed up entries.
     4. Server finishes administrative tasks:
            - hard links all files that haven't changed from the previous backup to the current backup
            - symlinks all entries that are symlinks on the remote server to their specified targets
            - copies files from the previous backup to the current if maxLinks is exceeeded
            - sets the mtime on all directories in the newly created backup
     */
    
    try {
        DEBUG(D_any) DFMT("current: " << currentDir);
        DEBUG(D_any) DFMT("previous: " << prevDir);
        
        // record number of filesystems the client is going to send (again, not really "filesystems")
        auto totalFS = client.ipcRead();
        int completeFS = 0;

        currentDir += tempExtension;
        string screenMessage = config.ifTitle() + " backing up to temp dir " + currentDir + "... ";
        string backspaces = string(screenMessage.length(), '\b');
        string blankspaces = string(screenMessage.length() , ' ');
        NOTQUIET && ANIMATE && cout << screenMessage << flush;
        DEBUG(D_any) cerr << "\n";
        DEBUG(D_netproto) DFMT("faub server ready to receive");
        NOTQUIET && ANIMATE && cout << progressPercentageA((int)totalFS, 7, completeFS, 0) << flush;

        log(config.ifTitle() + " starting backup to " + currentDir);
        GLOBALS.interruptFilename = currentDir;  // interruptFilename gets cleaned up on SIGTERM & SIGINT
        bool incTime = str2bool(config.settings[sIncTime].value);
        
        // all modified files from this client (used to create --diff list)
        set<string> modifiedFiles;
        
        /* loop through filesystems */
        do {
            fsTime.start();

            // needed (i.e. modified) files for this filesystem (pass of the protocol)
            set<string> neededFiles;
            
            // mtimes for all directories so we can set them at the very end
            map<string, time_t> dirMtimes;
            
            // hardlinks to create after receiving new files
            map<string,string> hardLinkList;
            
            // symlinks to create (bc they exist on the remote system) after receiving new files
            map<string,string> symLinkList;
            
            // files to copy from previous backup due to reaching maxLinks
            map<string,string> duplicateList;
            
            // total size of files neede from client for this fs - used for progress bar
            long fsTotalBytesNeeded = 0;
            long fsBytesReceived = 0;
            
            /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
             * phase 1 - get list of filenames and mtimes from client
             * and see if the remote file is different from what we
             * have locally in the most recent backup.
             */
            
            string fs = client.ipcReadTo(NET_DELIM);
            
            unsigned int maxLinksAllowed = config.settings[sMaxLinks].ivalue();
            size_t checkpointTotal = fileTotal;
            
            /* loop through files in this filesystem */
            while (1) {
                remoteFilename = client.ipcReadTo(NET_DELIM);
                if (remoteFilename == NET_OVER) {
                    break;
                }
                
                // log errors sent by the client
                if (remoteFilename.substr(0, 4) == "##* ") {
                    remoteFilename.erase(0, 4);
                    log(config.ifTitle() + " " + fs + " " + remoteFilename);
                    cleanupAndExitOnError();
                }
                
                ++fileTotal;
                long mtime = client.ipcRead();
                long mode  = client.ipcRead();
                long size = client.ipcRead();
                
                DEBUG(D_netproto) DFMTNOENDL("server learned about " << remoteFilename << " (" << to_string(mode) << ") ");
                
                localPrevFilename = slashConcat(prevDir, remoteFilename);
                localCurFilename = slashConcat(currentDir, remoteFilename);
                
                /*
                 * What do we need from the client based on the directory entry they've just described?
                 * The general process is to compare the remote filename to the local filename in the
                 * most recent backup and if both exist, compare their mtimes.  If there's a match we
                 * can assume the data hasn't changed. When they match we hard link the new file to
                 * the previous backup's copy of it.  Because of the order entries come over we can't
                 * guarantee that a parent directory will come over before a file within it.  So we
                 * make a list of all the entries as they come over the wire then go back and create
                 * the links at the end.  Details inline below.
                 
                 * The nuances of directories in this model can be incredibly confusing.  When you
                 * stumble upon a subdir you could examine its mtime and decide if its changed,
                 * effectively treating it just like a file (trasnfer its metadata only if necessary).
                 * Or you could say just transfer them all regardless of mtime (its only uid, gid,
                 * mode and mtime).  Either way you're getting files and directories in a non-intuitive
                 * order.  The problem is every time you add a file (or subdir) to a directory, you
                 * update that directory's mtime.  So if you want an accurate backup --including the
                 * mtimes on all your directories-- you need to reset those directory mtimes last.
                 * Either you track every directory where you've added a file/subdir or just do them all.
                 * I've elected to do them all because nearly every directory (except empty ones) are
                 * going to have this issue, regardless of whether the entries in them are newly
                 * transfered files or existing hardlinked ones.
                 */
                
                // if it's a directory, request it.  hardlinks don't work for directories. we don't
                // have this mtime at this point in the protocol so we just add it to the list to
                // request from the client.  when the client actually sends all its data, we'll save
                // the mtime for processing at the very end.
                if (S_ISDIR(mode)) {
                    neededFiles.insert(neededFiles.end(), remoteFilename);
                    ++unmodDirs;
                    DEBUG(D_netproto) DFMTNOPREFIX("[dir]");
                }
                else {
                    // lstat the previous backup's copy of the file and compare the mtimes
                    int statResult = mylstat(localPrevFilename, &statData);
                    
                    if (prevDir.length() && !statResult && statData.st_mtime == mtime) {
                        
                        /* check that hard links aren't maxed out against the configured limit.
                         * if we're at the limit & the backup includes the Time field OR
                         * if we're at the limit & the backup doesn't include Time & the new file doesn't exist
                         * then we mark this as a dup.  i.e. one we have to copy instead of hardlink.
                         */
                        struct stat statData2;
                        if ((statData.st_nlink >= maxLinksAllowed) &&
                            ((!incTime && mylstat(localCurFilename, &statData2)) || incTime)) {
                            duplicateList.insert(duplicateList.end(), pair<string, string>(localPrevFilename, localCurFilename));
                            ++maxLinksReached;
                            DEBUG(D_netproto) DFMTNOPREFIX("[matches, but links maxed]");
                        }
                        else {
                            // if they match then add it to the appropriate list to be symlinked or hardlinked, depending
                            // on whether its a symlink on the remote system
                            if (S_ISLNK(mode)) {
                                symLinkList.insert(symLinkList.end(), pair<string, string>(localPrevFilename, localCurFilename));
                                DEBUG(D_netproto) DFMTNOPREFIX("[remote symlink]");
                            }
                            else {
                                hardLinkList.insert(hardLinkList.end(), pair<string, string>(localPrevFilename, localCurFilename));
                                DEBUG(D_netproto) DFMTNOPREFIX("[matches, can hardlink]");
                            }
                        }
                    }
                    else {
                        // if the mtimes don't match or the file doesn't exist in the previous backup, add it to the list of
                        // ones we need the client to send in full
                        fsTotalBytesNeeded += size;
                        neededFiles.insert(neededFiles.end(), remoteFilename);
                        modifiedFiles.insert(modifiedFiles.end(), remoteFilename);
                        DEBUG(D_netproto) DFMTNOPREFIX("[" << (!prevDir.length() ? "no prev dir" : statResult < 0 ? "unable to stat " + localPrevFilename :
                                                               string("mtime mismatch (") + to_string(statData.st_mtime) + "; " + to_string(mtime)) << "]");
                    }
                }
            }
            
            NOTQUIET && ANIMATE && cout << progressPercentageA((int)totalFS, 7, completeFS, 1) << flush;
            DEBUG(D_netproto) DFMT(fs << " server phase 1 complete; total:" << fileTotal << ", need:" << neededFiles.size()
                                   << ", willLink:" << hardLinkList.size());
            
            
            /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
             * phase 2 - send the client the list of files that we need full copies of
             * because they've changed or are missing from the previous backup.
             * this includes every directory regardless of it changed.
             */
            for (auto &file: neededFiles) {
                DEBUG(D_netproto) DFMT("server requesting " << file);
                client.ipcWrite(string(file + NET_DELIM).c_str());
            }
            
            // tell the client we're done requesting and ready to listen to the replies
            client.ipcWrite(NET_OVER_DELIM);
            
            NOTQUIET && ANIMATE && cout << progressPercentageA((int)totalFS, 7, completeFS, 2) << flush;
            DEBUG(D_netproto) DFMT(fs << " server phase 2 complete; told client we need " << neededFiles.size() << " of " << fileTotal);
            
            
            /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
             * phase 3 - receive full copies of the files we've requested. they come over
             * in the order we've requested in the format of 8 bytes each for uid, gid,
             * mode, mtime, size and then the data ('size' number of bytes).
             */
            string label = ": transferred ";
            auto backs = string(label.length(), '\b');
            auto blanks = string(label.length(), ' ');
            cout << label;
            
            for (auto &file: neededFiles) {
                DEBUG(D_netproto) DFMT("server waiting for " << file);
                auto currentFilename = slashConcat(currentDir, file);
                auto [errorMsg, mode, mtime, size] = client.ipcReadToFile(currentFilename, !incTime);
                fsBytesReceived += size;

                if (S_ISDIR(mode))
                    dirMtimes.insert(dirMtimes.end(), make_pair(currentFilename, mtime));
                
                if (errorMsg.length()) {
                    SCREENERR(fs << " " << errorMsg);
                    log(config.ifTitle() + " " + fs + errorMsg);
                }
                
                if (mode < 1)
                    ++linkErrors;
                else
                    if (S_ISLNK(mode))
                        ++receivedSymLinks;
            
                if (fsTotalBytesNeeded > 1000000)
                    NOTQUIET && ANIMATE && cout << progressPercentageB(fsTotalBytesNeeded, fsBytesReceived) << flush;
            }
            
            if (fsTotalBytesNeeded > 1000000)
                NOTQUIET && ANIMATE && cout << progressPercentageB((long)0, (long)0) << flush;
            
            NOTQUIET && ANIMATE && cout << backs << blanks << backs << progressPercentageA((int)totalFS, 7, completeFS, 3) << flush;
            DEBUG(D_netproto) DFMT(fs << " server phase 3 complete; received " << plural((int)neededFiles.size(), "file") + " from client");
            
            
            /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
             * phase 4 - post-administrative work.
             * create the hardlinks for everything that matches the previous backup,
             * symlinks for everything that's a symlink on the remote server, copy
             * files from the previous backup when maxLinks is reached, and set the
             * mtime on all directories.
             */
            
            /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
             create hard links
             *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/
            for (auto &links: hardLinkList) {
                mkbasedirs(links.second);
                
                // when Time isn't included we're potentially overwriting an existing backup. pre-delete
                // so we don't get an error.
                if (!incTime)
                    unlink(links.second.c_str());
                
                if (link(links.first.c_str(), links.second.c_str()) < 0) {
                    ++linkErrors;
                    SCREENERR(fs << " error: unable to link " << links.second << " to " << links.first << " - " << strerror(errno));
                    log(config.ifTitle() + " " + fs + " error: unable to link " + links.second + " to " + links.first + " - " + strerror(errno));
                }
            }
            NOTQUIET && ANIMATE && cout << progressPercentageA((int)totalFS, 7, completeFS, 4) << flush;

            /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
             duplicate (copy) files for maxLinks
             *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/
            for (auto &dups: duplicateList) {
                mkbasedirs(dups.second);
                
                if (!copyFile(dups.first, dups.second)) {
                    ++linkErrors;
                    SCREENERR(fs << " error: unable to copy (attempted due to maxed out links) " << dups.first << " to " << dups.second << " - " << strerror(errno));
                    log(config.ifTitle() + " " + fs + " error: unable to copy (attempted due to maxed out links) " + dups.first + " to " + dups.second + " - " + strerror(errno));
                }
                else {
                    struct stat statData;
                    if (!mylstat(dups.first, &statData))
                        setFilePerms(dups.second, statData, false);
                }
            }
            NOTQUIET && ANIMATE && cout << progressPercentageA((int)totalFS, 7, completeFS, 5) << flush;

            /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
             create symlinks
             *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/
            char linkBuf[1000];
            for (auto &links: symLinkList) {
                mkbasedirs(links.second);
                
                // when Time isn't included we're potentially overwriting an existing backup. pre-delete
                // so we don't get an error.
                if (!incTime)
                    unlink(links.second.c_str());
                
                auto bytes = readlink(links.first.c_str(), linkBuf, sizeof(linkBuf));
                if (bytes >= 0 && bytes < sizeof(linkBuf)) {
                    linkBuf[bytes] = 0;
                    if (!symlink(linkBuf, links.second.c_str())) {
                        if (!mylstat(links.first, &statData)) {
                            if (lchown(links.second.c_str(), statData.st_uid, statData.st_gid)) {
                                SCREENERR(fs << " error: unable to chown symlink " << links.second << ": " << strerror(errno));
                                log(config.ifTitle() + " " + fs + " error: unable to chown symlink " + links.second + ": " + strerror(errno));
                            }
                            
                            struct timeval tv[2];
                            tv[0].tv_sec  = tv[1].tv_sec  = statData.st_mtime;
                            tv[0].tv_usec = tv[1].tv_usec = 0;
                            lutimes(links.second.c_str(), tv);
                        }
                    }
                    else {
                        ++linkErrors;
                        SCREENERR(fs << " error: unable to symlink " << links.second << " to " << links.first << ": " << strerror(errno));
                        log(config.ifTitle() + " " + fs + " error: unable to symlink " + links.second + " to " + links.first + ": " + strerror(errno));
                    }
                }
                else {
                    ++linkErrors;
                    SCREENERR(fs << " error: unable to dereference symlink " << links.first << ": " << strerror(errno));
                    log(config.ifTitle() + " " + fs + " error: unable to dereference symlink " + links.first + ": " + strerror(errno));
                }
                
            }
            NOTQUIET && ANIMATE && cout << progressPercentageA((int)totalFS, 7, completeFS, 6) << flush;

            /*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
             set mtimes on all directories
             *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/
            struct utimbuf timeBuf;
            for (auto &dirTime: dirMtimes) {
                timeBuf.actime = timeBuf.modtime = dirTime.second;
                if (utime(dirTime.first.c_str(), &timeBuf))
                    SCREENERR(log(config.ifTitle() + " " + fs + ": error: unable to call utime() on " + dirTime.first + " - " + strerror(errno)));
            }

            NOTQUIET && ANIMATE && cout << progressPercentageA((int)totalFS, 7, completeFS, 7) << flush;
            DEBUG(D_netproto) DFMT(fs << " server phase 4 complete; created " << plural(hardLinkList.size() - linkErrors, "link")  <<
                                   " to previously backed up files" << (linkErrors ? string(" (" + plural(linkErrors, "error") + ")") : ""));
            log(config.ifTitle() + " processed " + fs + ": " + plurali((int)fileTotal - checkpointTotal, "entr") + ", " +
                plural(neededFiles.size(), "request") + ", " + plural(hardLinkList.size() - linkErrors, "link"));
            
            filesModified += neededFiles.size();
            filesHardLinked += hardLinkList.size();
            filesSymLinked += symLinkList.size();
            ++completeFS;
            
            fsTime.stop();
            
            // if it's been more than 5 minutes and no files are found (modified, unmodified, anything)
            // then we have a timeout error - such as when the OS prompts for permission to read a
            // protected directory but there's no user around to answer.  we have to finish the network
            // conversation to let the client instance terminate, then we'll blow away the failed backup
            // on the server side.
            if (!filesModified && !filesHardLinked && !filesSymLinked && fsTime.seconds() > 600)
                abortBackupAtEnd = true;
            
        } while (client.ipcRead());

        // note finish time
        backupTime.stop();
        NOTQUIET && ANIMATE && cout << progressPercentageA((int)0, (int)0) << backspaces << blankspaces << backspaces << flush;
        
        if (abortBackupAtEnd) {
            string errorDetail = config.ifTitle() + " backup aborted due to timeout on the faub client end";
            log(errorDetail);
            SCREENERR(errorDetail);
            notify(config, "\t• " + errorDetail, false);
            rmrf(currentDir);
            return;
        }
        
        // if time isn't included we may be about to overwrite a previous backup for this date
        if (!incTime)
            rmrf(originalCurrentDir);
        
        if (rename(string(currentDir).c_str(), originalCurrentDir.c_str())) {
            if (!filesModified && !filesHardLinked && !filesSymLinked) {
                log(config.ifTitle() + " no files available to backup; backup aborted");
                NOTQUIET && cout << "\t• " << config.ifTitle() << " no files to backup" << endl;
                return;
            }
                
            string errorDetail = config.ifTitle() + " unable to rename " + currentDir + " to " + originalCurrentDir + ": " + strerror(errno);
            log(errorDetail);
            SCREENERR(errorDetail);
            notify(config, "\t• " + errorDetail, false);
            return;
        }
        
        GLOBALS.interruptFilename = "";
        currentDir = originalCurrentDir;
        
        // add the backup to the cache, including running a dus()
        config.fcache.recache(currentDir);
        
        // record which files changed in this backup
        config.fcache.updateDiffFiles(currentDir, modifiedFiles);
        
        // we can pull these out to display
        auto fcacheCurrent = config.fcache.getBackupByDir(currentDir);
        auto backupSize = fcacheCurrent->second.ds.getSize();
        auto backupSaved = fcacheCurrent->second.ds.getSaved();
        
        // and only need to update the remaining fields
        fcacheCurrent->second.duration = backupTime.seconds();
        fcacheCurrent->second.finishTime = time(NULL);
        fcacheCurrent->second.modifiedFiles = filesModified - unmodDirs;
        fcacheCurrent->second.unchangedFiles = filesHardLinked;
        fcacheCurrent->second.dirs = unmodDirs;
        fcacheCurrent->second.slinks = filesSymLinked + receivedSymLinks;

        string maxLinkMsg = maxLinksReached ? " [" + plural(maxLinksReached, "max link") + " reached]" : "";
        string message1 = "backup completed to " + currentDir + " in " + backupTime.elapsed();
        string message2 = "(total: " +
            to_string(fileTotal) + ", modified: " + to_string(filesModified - unmodDirs) + ", unmodified: " + to_string(filesHardLinked) + ", dirs: " +
            to_string(unmodDirs) + ", symlinks: " + to_string(filesSymLinked + receivedSymLinks) +
            (linkErrors ? ", linkErrors: " + to_string(linkErrors) : "") +
            ", size: " + approximate(backupSize + backupSaved) + ", usage: " + approximate(backupSize) + maxLinkMsg + ")";
                        
        log(config.ifTitle() + " " + message1);
        log(config.ifTitle() + " " + message2);
        NOTQUIET && cout << "\t• " << config.ifTitle() << " " << message1 << "\n\t\t" << message2 << endl;
        
        if (config.settings[sBloat].value.length()) {
            string bloat = config.settings[sBloat].value;
            auto target = config.getBloatTarget();
            if (fcacheCurrent->second.ds.sizeInBytes > target) {
                string message = config.ifTitle() + " warning: backup is larger than the bloat threshold (backup usage: " + approximate(fcacheCurrent->second.ds.sizeInBytes) + ", threshold: " + bloat + ", target: " + approximate(target);
                log(message + ")");
                SCREENERR(message + ")")
                message += maxLinkMsg + ")";
                notify(config, "\t• " + message, false);
                return;
            }
        }

        notify(config, "\t• " + message1 + "\n\t\t" + message2 + "\n", true);
    }
    catch (MBException &e) {
        notify(config, "\t• " + config.ifTitle() + " Error (exception): " + e.detail(), false);
        log(config.ifTitle() + "  error (exception): " + e.detail());
        SCREENERR("error (exception): " + e.detail());
        rmrf(currentDir);
    }
    catch (...) {
        notify(config, "\t• " + config.ifTitle() + " Error (exception): unknown", false);
        log(config.ifTitle() + "  error (exception), unknown");
        SCREENERR("error (exception): unknown");
        rmrf(currentDir);
    }
}


struct scanToServerDataType {
    size_t totalEntries;
    IPC_Base *server;
};


bool scanToServerCallback(pdCallbackData &file) {
    scanToServerDataType *data = (scanToServerDataType*)file.dataPtr;
    
    data->totalEntries++;
    data->server->ipcWrite(string(file.filename + NET_DELIM).c_str());
    data->server->ipcWrite(file.statData.st_mtime);
    data->server->ipcWrite(file.statData.st_mode);
    data->server->ipcWrite(file.statData.st_size);
    DEBUG(D_netproto) DFMT("client provided stats on " << file.filename);
    
    return true;
}


/*
 * fc_scanToServer() - faub client
 * Scan a filesystem, sending the filenames and their associated mtime's back
 * to the remote server. This is the client's side of phase 1.
 */
size_t fc_scanToServer(BackupConfig& config, string entryName, IPC_Base& server) {
    scanToServerDataType data;
    data.server = &server;
    data.totalEntries = 0;
    
    string clude = config.settings[sInclude].value.length() ? config.settings[sInclude].value : config.settings[sExclude].value.length() ? config.settings[sExclude].value : "";

    entryName.erase(remove(entryName.begin(), entryName.end(), '\\'), entryName.end());
    processDirectory(entryName, clude, config.settings[sExclude].value.length(), config.settings[sFilterDirs].value.length(), scanToServerCallback, &data);
    
    return data.totalEntries;
}

/*
 * fc_sendFilesToServer() - faub client
 * Receive a list of files from the server (client side of phase 2) and
 * send each file back to the server (client side of phase 3).
 */
size_t fc_sendFilesToServer(IPC_Base& server) {
    vector<string> neededFiles;
    
    while (1) {
        string filename = server.ipcReadTo(NET_DELIM);
        
        if (filename == NET_OVER)
            break;
        
        DEBUG(D_netproto) DFMT("client received request for " << filename);
        neededFiles.insert(neededFiles.end(), filename);
    }
    
    DEBUG(D_netproto) DFMT("client received requests for " << to_string(neededFiles.size()) << " file(s)");
    
    for (auto &file: neededFiles) {
        DEBUG(D_netproto) DFMT("client sending " << file << " to server");
        server.ipcSendDirEntry(file);
    }

    return neededFiles.size();
}


void fc_mainEngine(BackupConfig& config, vector<string> origPaths) {
    try {
        IPC_Base server(0, 1, 60);  // use stdin and stdout

        vector<string> paths;
        
        // the vector of paths that come into the function can be from the profile (conf file).
        // each item could be a quoted list of multiple sub paths.  so we have to break it down
        // a second time.
        for (auto &p : origPaths) {
            auto dirVec = string2vector(p, true, true);
            
            for (auto &d : dirVec) {
                auto fileVec = expandWildcardFilespec(d);
                paths.insert(paths.end(), fileVec.begin(), fileVec.end());
            }
        }
        
        DEBUG(D_faub) DFMT("faub client starting with " << paths.size() << " request(s)");

        // tell server the number of filesystems we're going to process
        server.ipcWrite((__int64_t)paths.size());

        for (auto it = paths.begin(); it != paths.end(); ++it) {
            timer clientTime;
            clientTime.start();
            
            server.ipcWrite(string(*it + NET_DELIM).c_str());
            auto entries = fc_scanToServer(config, *it, server);

            server.ipcWrite(NET_OVER_DELIM);
            auto requests = fc_sendFilesToServer(server);

            clientTime.stop();
            log("faub_client request for " + *it + " served " + plurali(entries, "entr") +
                ", " + plural(requests, "request") + " in " + clientTime.elapsed());

            __int64_t end = (it+1) != paths.end();
            server.ipcWrite(end);
        }
        
        DEBUG(D_netproto) DFMT("client complete.");
    }
    catch (string s) {
        cerr << "faub client caught internal exception: " << s << endl;
        log("error: faub client caught internal exception: " + s);
    }
    catch (MBException &e) {
        cerr << "faub client caught MB excetion: " << e.detail() << endl;
        log("error: faub client caught exception: " + e.detail());
    }
    catch (...) {
        cerr << "faub client caught unknown exception" << endl;
        log("error: faub client caught unknown exception");
    }
}
