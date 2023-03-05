#include <dirent.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <algorithm>
#include <unistd.h>

#include "FaubCache.h"
#include "faub.h"
#include "ipc.h"
#include "notify.h"
#include "debug.h"
#include "globals.h"
#include "exception.h"


extern void sigTermHandler(int sig);

tuple<string, time_t> mostRecentBackupDirSinceInternal(int baseSlashes, string backupDir, time_t sinceTime, string profileName);


/*
 * mostRecentBackupDirSince(backupBaseDir, sinceDir)
 *
 * backupBaseDir is the backup dir root (e.g. /tmp/mybackups)
 * sinceDir is a specific full backup path (e.g. /tmp/mybackups/2023/012/the-backup-20230106)
 */
string mostRecentBackupDirSince(int baseSlashes, string backupBaseDir, string sinceDir, string profileName) {
    struct stat statData;
    time_t sinceTime = 0;

    ++GLOBALS.statsCount;
    if (!stat(sinceDir.c_str(), &statData))
        sinceTime = statData.st_mtime;
    else
        sinceTime = time(NULL);

    auto [fname, fmtime] = mostRecentBackupDirSinceInternal(baseSlashes, backupBaseDir, sinceTime, profileName);
    return fname;
}


tuple<string, time_t> mostRecentBackupDirSinceInternal(int baseSlashes, string backupDir, time_t sinceTime, string profileName) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    struct stat statData;
    string recentName;
    time_t recentTime = 0;
    vector<string> subDirs;
    
    Pcre matchSpec(DATE_REGEX);
    if (matchSpec.search(backupDir)) {

        ++GLOBALS.statsCount;
        if (!stat(ue(backupDir).c_str(), &statData)) {
            DEBUG(D_faub) DFMT("returning " << backupDir);
            return {backupDir, statData.st_mtime};
        }

        return {"", 0};
    }

    if ((c_dir = opendir(ue(backupDir).c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {

            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
               continue;

            string fullFilename = backupDir + "/" + string(c_dirEntry->d_name);

            ++GLOBALS.statsCount;
            if (!stat(fullFilename.c_str(), &statData)) {

                if (S_ISDIR(statData.st_mode)) {
                    auto slashDiff = count(fullFilename.begin(), fullFilename.end(), '/') - baseSlashes;

                    if (slashDiff < 3 || (slashDiff == 3 && 
                                string(c_dirEntry->d_name).length() == 2 && isdigit(c_dirEntry->d_name[0]) && isdigit(c_dirEntry->d_name[1]))) {
                        DEBUG(D_faub) DFMT("adding " << fullFilename << " to recursion list");
                        subDirs.insert(subDirs.end(), fullFilename);
                    }
                    
                    if (slashDiff > 2 && slashDiff < 5) {
                        // next we make sure the subdir matches our profile name
                        if (fullFilename.find(profileName) != string::npos) {
                            DEBUG(D_faub) DFMT("name match on " << fullFilename << ", checking times");
                           
                            if ((statData.st_mtime > recentTime) && ((statData.st_mtime < sinceTime) || !sinceTime)) {
                                DEBUG(D_faub) DFMT("valid times on " << fullFilename << ", noting");
                                recentTime = statData.st_mtime;
                                recentName = fullFilename;
                            }
                        }
                    }
                }
            }
        }
        closedir(c_dir);
        
        for (auto &dir: subDirs) {
            auto [fname, fmtime] = mostRecentBackupDirSinceInternal(baseSlashes, dir, sinceTime, profileName);
            if ((fmtime > recentTime) && ((fmtime < sinceTime) || !sinceTime)) {
                recentTime = fmtime;
                recentName = fname;
            }
        }
    }

    return {recentName, recentTime};
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

    auto baseSlashes = (int)count(config.settings[sDirectory].value.begin(), config.settings[sDirectory].value.end(), '/');
    string newDir = newBackupDir(config);
    string prevDir = mostRecentBackupDirSince(baseSlashes, config.settings[sDirectory].value, newDir, config.settings[sTitle].value);

    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW << config.ifTitle() << " TESTMODE: would have begun backup by executing \"" << config.settings[sFaub].value << "\"" << endl;
        cout << "saving to " << newDir << endl;
        cout << "comparing to previous " << prevDir << RESET << endl;
        return;
    }

    DEBUG(D_netproto) DFMT("executing: \"" << config.settings[sFaub].value << "\"");
    faub.execute("faub", false, false, true);
    fs_serverProcessing(faub, config, prevDir, newDir);
}


#define PB(x) string(5 + to_string(x).length(), '\b')
void fs_serverProcessing(PipeExec& client, BackupConfig& config, string prevDir, string currentDir) {
    string remoteFilename;
    string localPrevFilename;
    string localCurFilename;
    string originalCurrentDir = currentDir;
    struct stat statData;
    ssize_t fileTotal = 0;
    ssize_t filesModified = 0;
    ssize_t filesHardLinked = 0;
    ssize_t filesSymLinked = 0;
    ssize_t receivedSymLinks = 0;
    ssize_t unmodDirs = 0;
    unsigned int linkErrors = 0;
    string tempExtension = ".tmp." + to_string(GLOBALS.pid);

    // note start time
    timer backupTime;
    backupTime.start();

    try {
        DEBUG(D_faub) DFMT("current: " << currentDir);
        DEBUG(D_faub) DFMT("previous: " << prevDir);
        
        // record number of filesystems the client is going to send
        auto numFS = client.ipcRead();
        
        currentDir += tempExtension;
        string screenMessage = config.ifTitle() + " backing up to temp dir " + currentDir + " (" + to_string(numFS * 4) +  ")... ";
        string backspaces = string(screenMessage.length(), '\b');
        string blankspaces = string(screenMessage.length() , ' ');
        NOTQUIET && ANIMATE && cout << screenMessage << flush;
        DEBUG(D_any) cerr << "\n";
        DEBUG(D_netproto) DFMT("faub server ready to receive");
        
        log(config.ifTitle() + " starting backup to " + currentDir);
        GLOBALS.interruptFilename = currentDir;  // interruptFilename gets cleaned up on SIGTERM & SIGINT
        bool incTime = str2bool(config.settings[sIncTime].value);
        
        set<string> modifiedFiles;
        
        do {
            set<string> neededFiles;
            map<string,string> hardLinkList;
            map<string,string> symLinkList;
            map<string,string> duplicateList;
            
            /*
             * phase 1 - get list of filenames and mtimes from client
             * and see if the remote file is different from what we
             * have locally in the most recent backup.
             */
            
            string fs = client.ipcReadTo(NET_DELIM);
            
            unsigned int maxLinksAllowed = config.settings[sMaxLinks].ivalue();
            size_t checkpointTotal = fileTotal;
            
            while (1) {
                remoteFilename = client.ipcReadTo(NET_DELIM);
                if (remoteFilename == NET_OVER) {
                    break;
                }
                
                // log errors sent by the client
                if (remoteFilename.substr(0, 4) == "##* ") {
                    remoteFilename.erase(0, 4);
                    log(config.ifTitle() + " " + fs + " " + remoteFilename);
                    sigTermHandler(0);
                    exit(10);
                }
                
                ++fileTotal;
                long mtime = client.ipcRead();
                long mode  = client.ipcRead();
                
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
                 */
                
                // if it's a directory, we need it.  hardlinks don't work for directories.
                if (S_ISDIR(mode)) {
                    neededFiles.insert(neededFiles.end(), remoteFilename);
                    ++unmodDirs;
                    DEBUG(D_netproto) DFMTNOPREFIX("[dir]");
                }
                else {
                    // lstat the previous backup's copy of the file and compare the mtimes
                    ++GLOBALS.statsCount;
                    int statResult = lstat(localPrevFilename.c_str(), &statData);
                    
                    if (prevDir.length() && !statResult && statData.st_mtime == mtime) {
                        
                        /* check that hard links aren't maxed out against the configured limit.
                         * if we're at the limit & the backup includes the Time field OR
                         * if we're at the limit & the backup doesn't include Time & the new file doesn't exist
                         * then we mark this as a dup.  i.e. one we have to copy instead of hardlink.
                         */
                        struct stat statData2;
                        if ((statData.st_nlink >= maxLinksAllowed) &&
                            ((!incTime && lstat(localCurFilename.c_str(), &statData2)) || incTime)) {
                            if (!incTime) ++GLOBALS.statsCount;
                            duplicateList.insert(duplicateList.end(), pair<string, string>(localPrevFilename, localCurFilename));
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
                        neededFiles.insert(neededFiles.end(), remoteFilename);
                        modifiedFiles.insert(modifiedFiles.end(), remoteFilename);
                        DEBUG(D_netproto) DFMTNOPREFIX("[" << (!prevDir.length() ? "no prev dir" : statResult < 0 ? "unable to stat" :
                                                               string("mtime mismatch (") + to_string(statData.st_mtime) + "; " + to_string(mtime)) << "]");
                    }
                }
            }
            
            NOTQUIET && ANIMATE && cout << PB(numFS * 4) << to_string(numFS * 4 - 1) << ")... " << flush;
            DEBUG(D_netproto) DFMT(fs << " server phase 1 complete; total:" << fileTotal << ", need:" << neededFiles.size()
                                   << ", willLink:" << hardLinkList.size());
            
            /*
             * phase 2 - send the client the list of files that we need full copies of
             * because they've changed or are missing from the previous backup
             */
            for (auto &file: neededFiles) {
                DEBUG(D_netproto) DFMT("server requesting " << file);
                client.ipcWrite(string(file + NET_DELIM).c_str());
            }
            
            client.ipcWrite(NET_OVER_DELIM);
            
            NOTQUIET && ANIMATE && cout << PB(numFS * 4 - 1) << to_string(numFS * 4 - 2) << ")... " << flush;
            DEBUG(D_netproto) DFMT(fs << " server phase 2 complete; told client we need " << neededFiles.size() << " of " << fileTotal);
            
            /*
             * phase 3 - receive full copies of the files we've requested. they come over
             * in the order we've requested in the format of 8 bytes for 'size' and then
             * the 'size' number of bytes of data.
             */
            for (auto &file: neededFiles) {
                DEBUG(D_netproto) DFMTNOENDL("server waiting for " << file);
                auto [errorMsg, mode] = client.ipcReadToFile(slashConcat(currentDir, file), !incTime);
                DEBUG(D_netproto) DFMTNOPREFIX(" :mode " << mode);
                
                if (errorMsg.length()) {
                    SCREENERR(fs << " " << errorMsg);
                    log(config.ifTitle() + " " + fs + errorMsg);
                }
                
                if (mode < 1)
                    ++linkErrors;
                else
                    if (S_ISLNK(mode))
                        ++receivedSymLinks;
            }
            
            NOTQUIET && ANIMATE && cout << PB(numFS * 4 - 2) << to_string(numFS * 4 - 3) << ")... " << flush;
            DEBUG(D_netproto) DFMT(fs << " server phase 3 complete; received " << plural((int)neededFiles.size(), "file") + " from client");
            
            /*
             * phase 4 - create the links for everything that matches the previous backup.
             */
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
            
            for (auto &dups: duplicateList) {
                mkbasedirs(dups.second);
                
                if (!copyFile(dups.first, dups.second)) {
                    ++linkErrors;
                    SCREENERR(fs << " error: unable to copy (due to maxed out links) " << dups.first << " to " << dups.second << " - " << strerror(errno));
                    log(config.ifTitle() + " " + fs + " error: unable to copy (due to maxed out links) " + dups.first + " to " + dups.second + " - " + strerror(errno));
                }
                else {
                    struct stat statData;
                    ++GLOBALS.statsCount;
                    if (!lstat(dups.first.c_str(), &statData)) {
                        if (lchown(dups.second.c_str(), statData.st_uid, statData.st_gid)) {
                            SCREENERR(fs << " error: unable to chown " << dups.second << ": " << strerror(errno));
                            log(config.ifTitle() + " " + fs + " error: unable to chown " + dups.second + ": " + strerror(errno));
                        }
                        
                        if (chmod(dups.second.c_str(), statData.st_mode)) {
                            SCREENERR(fs << " error: unable to chmod " << dups.second << ": " << strerror(errno));
                            log(config.ifTitle() + " " + fs + " error: unable to chmod " + dups.second + ": " + strerror(errno));
                        }
                        
                        struct timeval tv[2];
                        tv[0].tv_sec  = tv[1].tv_sec  = statData.st_mtime;
                        tv[0].tv_usec = tv[1].tv_usec = 0;
                        lutimes(dups.second.c_str(), tv);
                    }
                }
            }
            
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
                        ++GLOBALS.statsCount;
                        if (!lstat(links.first.c_str(), &statData)) {
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
            
            DEBUG(D_netproto) DFMT(fs << " server phase 4 complete; created " << plural(hardLinkList.size() - linkErrors, "link")  <<
                                   " to previously backed up files" << (linkErrors ? string(" (" + plural(linkErrors, "error") + ")") : ""));
            log(config.ifTitle() + " processed " + fs + ": " + plurali((int)fileTotal - checkpointTotal, "entr") + ", " +
                plural(neededFiles.size(), "request") + ", " + plural(hardLinkList.size() - linkErrors, "link"));
            
            filesModified += neededFiles.size();
            filesHardLinked += hardLinkList.size();
            filesSymLinked += symLinkList.size();
            --numFS;
            
        } while (client.ipcRead());
        
        // note finish time
        backupTime.stop();
        NOTQUIET && ANIMATE && cout << backspaces << blankspaces << backspaces << flush;
        
        // if time isn't included we may be about to overwrite a previous backup for this date
        if (!incTime)
            rmrf(originalCurrentDir.c_str());
        
        if (rename(string(currentDir).c_str(), originalCurrentDir.c_str())) {
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
        
        string message1 = "backup completed to " + currentDir + " in " + backupTime.elapsed();
        string message2 = "(total: " +
        to_string(fileTotal) + ", modified: " + to_string(filesModified - unmodDirs) + ", unmodified: " + to_string(filesHardLinked) + ", dirs: " +
        to_string(unmodDirs) + ", symlinks: " + to_string(filesSymLinked + receivedSymLinks) +
        (linkErrors ? ", linkErrors: " + to_string(linkErrors) : "") +
        ", size: " + approximate(backupSize + backupSaved) + ", usage: " + approximate(backupSize) + ")";
        log(config.ifTitle() + " " + message1);
        log(config.ifTitle() + " " + message2);
        NOTQUIET && cout << "\t• " << config.ifTitle() << " " << message1 << "\n\t\t" << message2 << endl;
        
        if (config.settings[sBloat].value.length()) {
            string bloat = config.settings[sBloat].value;
            auto target = config.getBloatTarget();
            if (fcacheCurrent->second.ds.sizeInBytes > target) {
                string message = config.ifTitle() + " warning: backup is larger than the bloat threshold (backup usage: " + approximate(fcacheCurrent->second.ds.sizeInBytes) + ", threshold: " + bloat + ", target: " + approximate(target) + ")";
                log(message);
                SCREENERR(message)
                notify(config, "\t• " + message, false);
                return;
            }
        }

        notify(config, "\t• " + message1 + "\n\t\t" + message2 + "\n", true);
    }
    catch (MBException &e) {
        notify(config, "\t• " + config.ifTitle() + " Error (exception): " + e.detail(), false);
        log(config.ifTitle() + "  error (exception): " + e.what());
    }
    catch (...) {
        notify(config, "\t• " + config.ifTitle() + " Error (exception): unknown", false);
        log(config.ifTitle() + "  error (exception), unknown");
    }
}


/*
 * fc_scanToServer()
 * Scan a filesystem, sending the filenames and their associated mtime's back
 * to the remote server. This is the client's side of phase 1.
 */
size_t fc_scanToServer(string entryName, IPC_Base& server) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    struct stat statData;
    size_t totalEntries = 0;
    vector<string> subDirs;
    
    entryName.erase(remove(entryName.begin(), entryName.end(), '\\'), entryName.end());
    
    ++GLOBALS.statsCount;
    if (!lstat(entryName.c_str(), &statData)) {
        if (S_ISDIR(statData.st_mode)) {
            
            if ((c_dir = opendir(ue(entryName).c_str())) != NULL) {
                while ((c_dirEntry = readdir(c_dir)) != NULL) {
                    
                    if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                        continue;
                    
                    string fullFilename = entryName + "/" + string(c_dirEntry->d_name);
                    
                    ++GLOBALS.statsCount;
                    if (!lstat(fullFilename.c_str(), &statData)) {
                        ++totalEntries;
                        server.ipcWrite(string(fullFilename + NET_DELIM).c_str());
                        server.ipcWrite(statData.st_mtime);
                        server.ipcWrite(statData.st_mode);
                        DEBUG(D_netproto) DFMT("client provided stats on " << fullFilename);
                        
                        if (S_ISDIR(statData.st_mode))
                            subDirs.insert(subDirs.end(), fullFilename);
                    }
                    else
                        log("error: stat failed for " + fullFilename);
                }
                closedir(c_dir);

                for (auto &dir: subDirs)
                    totalEntries += fc_scanToServer(dir, server);
            }
            else {
                log("error: can't open " + entryName + " - " + strerror(errno));
                server.ipcWrite(string(string("##* error: client instance unable to read ") + entryName + " - " + strerror(errno) + NET_DELIM).c_str());
                sigTermHandler(0);
                exit(5);
            }
        }
        else {
            // if one of the top-level "filesystems" / "directories" given to --path on the client
            // isn't a directory but instead a file, handle it here
            ++totalEntries;
            server.ipcWrite(string(entryName + NET_DELIM).c_str());
            server.ipcWrite(statData.st_mtime);
            server.ipcWrite(statData.st_mode);
            DEBUG(D_netproto) DFMT("client provided stats on " << entryName);
        }
    }
    
    return totalEntries;
}

/*
 * fc_sendFilesToServer()
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


void fc_mainEngine(vector<string> paths) {
    try {
        IPC_Base server(0, 1, 60);  // use stdin and stdout

        DEBUG(D_faub) DFMT("faub client starting with " << paths.size() << " request(s)");

        // tell server the number of filesystems we're going to process
        server.ipcWrite((__int64_t)paths.size());

        for (auto it = paths.begin(); it != paths.end(); ++it) {
            timer clientTime;
            clientTime.start();
            
            server.ipcWrite(string(*it + NET_DELIM).c_str());
            auto entries = fc_scanToServer(*it, server);

            server.ipcWrite(NET_OVER_DELIM);
            auto changes = fc_sendFilesToServer(server);

            clientTime.stop();
            log("faub_client request for " + *it + " served " + plurali((int)entries, "entr") +
                ", " + plural((int)changes, "change") + " in " + clientTime.elapsed());

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

    exit(1);
}


