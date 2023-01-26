#include "dirent.h"
#include "sys/stat.h"
#include <netinet/tcp.h>

#include "FaubCache.h"
#include "faub.h"
#include "PipeExec.h"
#include "notify.h"
#include "debug.h"
#include "globals.h"

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

    Pcre matchSpec("-\\d{8}($|-\\d{2}:)");
    if (matchSpec.search(backupDir)) {

        if (!stat(backupDir.c_str(), &statData)) {
            DEBUG(D_faub) DFMT("returning " << backupDir);
            return {backupDir, statData.st_mtime};
        }

        return {"", 0};
    }

    if ((c_dir = opendir(backupDir.c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {

            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
               continue;

            string fullFilename = backupDir + "/" + string(c_dirEntry->d_name);

            if (!stat(fullFilename.c_str(), &statData)) {

                if (S_ISDIR(statData.st_mode)) {
                    auto slashDiff = count(fullFilename.begin(), fullFilename.end(), '/') - baseSlashes;

                    if (slashDiff < 3 || (slashDiff == 3 && 
                                string(c_dirEntry->d_name).length() == 2 && isdigit(c_dirEntry->d_name[0]) && isdigit(c_dirEntry->d_name[1]))) {
                        DEBUG(D_faub) DFMT("recursing into " << fullFilename);
                        auto [fname, fmtime] = mostRecentBackupDirSinceInternal(baseSlashes, fullFilename, sinceTime, profileName);
                        if ((fmtime > recentTime) && ((fmtime < sinceTime) || !sinceTime)) {
                            recentTime = fmtime;
                            recentName = fname;
                        }
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
    }

    return {recentName, recentTime};
}


string newBackupDir(BackupConfig& config) {
    time_t rawTime;
    struct tm *timeFields;
    char buffer[100];

    time(&rawTime);
    timeFields = localtime(&rawTime);
    bool incTime = str2bool(config.settings[sIncTime].value);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d@%X", timeFields);

    string setDir = config.settings[sDirectory].value;
    //strftime(buffer, sizeof(buffer), incTime ? "%Y/%m/%d" : "%Y/%m", localtime(&rawTime));
    strftime(buffer, sizeof(buffer), incTime ? "%Y/%m/%d" : "%Y/%m", localtime(&rawTime));
    string subDir = buffer;

    strftime(buffer, sizeof(buffer), incTime ? "-%Y%m%d@%H:%M:%S" : "-%Y%m%d", localtime(&rawTime));
    string filename = buffer;

    string fullPath = slashConcat(config.settings[sDirectory].value, subDir, safeFilename(config.settings[sTitle].value) + filename);

    return fullPath;
}


void fs_startServer(BackupConfig& config) {
    PipeExec faub(config.settings[sFaub].value, 60);

    if (GLOBALS.cli.count(CLI_NOBACKUP))
        return;

    auto baseSlashes = count(config.settings[sDirectory].value.begin(), config.settings[sDirectory].value.end(), '/');
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

    DEBUG(D_faub) DFMT("current: " << currentDir);
    DEBUG(D_faub) DFMT("previous: " << prevDir);

    currentDir += tempExtension;
    string screenMessage = config.ifTitle() + " backing up to temp dir " + currentDir + " (phase 1/4)... ";
    string backspaces = string(screenMessage.length(), '\b');
    string phaseback = string(8, '\b');
    string blankspaces = string(screenMessage.length() , ' ');
    NOTQUIET && ANIMATE && cout << screenMessage << flush;
    DEBUG(D_any) cerr << "\n";
    DEBUG(D_netproto) DFMT("faub server ready to receive");

    log(config.ifTitle() + " starting backup to " + currentDir);

    do {
        set<string> neededFiles;
        map<string,string> hardLinkList;
        map<string,string> symLinkList;

        /*
         * phase 1 - get list of filenames and mtimes from client
         * and see if the remote file is different from what we
         * have locally in the most recent backup.
        */

        string fs = client.readTo(NET_DELIM);

        while (1) {
            remoteFilename = client.readTo(NET_DELIM);
            if (remoteFilename == NET_OVER) {
                break;
            }

            ++fileTotal;
            long mtime = client.readProc();
            long mode  = client.readProc();

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
                int statResult = lstat(localPrevFilename.c_str(), &statData);

                if (prevDir.length() && !statResult && statData.st_mtime == mtime) {
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
                else {
                    // if the mtimes don't match or the file doesn't exist in the previous backup, add it to the list of
                    // ones we need the client to send in full
                    neededFiles.insert(neededFiles.end(), remoteFilename);
                    DEBUG(D_netproto) DFMTNOPREFIX("[" << (!prevDir.length() ? "no prev dir" : statResult < 0 ? "unable to stat" : 
                            string("mtime mismatch (") + to_string(statData.st_mtime) + "; " + to_string(mtime)) << "]");
                }
            }
                
        }
 
        NOTQUIET && ANIMATE && cout << phaseback << "2/4)... " << flush;
        log(config.ifTitle() + " " + fs + " phase 1: client provided " + to_string(fileTotal) + " entr" + ies(fileTotal)); 
        DEBUG(D_netproto) DFMT(fs << " server phase 1 complete; total:" << fileTotal << ", need:" << neededFiles.size() 
                << ", willLink:" << hardLinkList.size());

        /*
         * phase 2 - send the client the list of files that we need full copies of
         * because they've changed or are missing from the previous backup
        */
        for (auto &file: neededFiles) {
            DEBUG(D_netproto) DFMT("server requesting " << file);
            client.writeProc(string(file + NET_DELIM).c_str());
        }

        client.writeProc(NET_OVER_DELIM);

        NOTQUIET && ANIMATE && cout << phaseback << "3/4)... " << flush;
        DEBUG(D_netproto) DFMT(fs << " server phase 2 complete; told client we need " << neededFiles.size() << " of " << fileTotal);
        log(config.ifTitle() + " " + fs + " phase 2: requested " + to_string(neededFiles.size()) + " entr" + ies(neededFiles.size()) + " from client");

        /*
         * phase 3 - receive full copies of the files we've requested. they come over
         * in the order we've requested in the format of 8 bytes for 'size' and then
         * the 'size' number of bytes of data.
        */
        bool incTime = str2bool(config.settings[sIncTime].value);
        for (auto &file: neededFiles) {
            DEBUG(D_netproto) DFMT("server waiting for " << file);
            auto fmode = client.readToFile(slashConcat(currentDir, file), !incTime);

            if (fmode == -1)
                ++linkErrors;
            else
                if (S_ISLNK(fmode))
                    ++receivedSymLinks;
        }

        NOTQUIET && ANIMATE && cout << phaseback << "4/4)... " << flush;
        DEBUG(D_netproto) DFMT(fs << " server phase 3 complete; received " << neededFiles.size() << " file" << s(neededFiles.size()) + " from client");
        log(config.ifTitle() + " " + fs + " phase 3: received " + to_string(neededFiles.size()) + " entr" + ies(neededFiles.size()) + " from client");

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

        DEBUG(D_netproto) DFMT(fs << " server phase 4 complete; created " << (hardLinkList.size() - linkErrors)  << 
                " link" << s(hardLinkList.size() - linkErrors) << " to previously backed up files" << (linkErrors ? string(" (" + to_string(linkErrors) + " error" + s(linkErrors) + ")") : ""));
        log(config.ifTitle() + " " + fs + " phase 4: created " + to_string(hardLinkList.size() - linkErrors) + " link" + s(hardLinkList.size() - linkErrors) + " (" + to_string(linkErrors) + " error" + s(linkErrors) + ")");

        filesModified += neededFiles.size();
        filesHardLinked += hardLinkList.size();
        filesSymLinked += symLinkList.size();

    } while (client.readProc());

    // note finish time
    backupTime.stop();
    NOTQUIET && ANIMATE && cout << backspaces << blankspaces << backspaces << flush;

    if (rename(string(currentDir).c_str(), originalCurrentDir.c_str())) {
        string errorDetail = config.ifTitle() + " unable to rename " + currentDir + " to " + originalCurrentDir + ": " + strerror(errno);
        log(errorDetail);
        SCREENERR(errorDetail);
        return;
    }

    currentDir = originalCurrentDir;

    // loading the cache for this baseDir will automatically detect our new backup having
    // no cached stats and run a dus() on it to determine totalSize/totalSaved.
    FaubCache fcache(config.settings[sDirectory].value, config.settings[sTitle].value);

    // the one exception is if this backup was already in the cache and this run is
    // overwriting that backup.  in that case we may need to recache() the new version.
    // recache() is smart enough to skip backups its already recached in this run of the
    // app.  i.e. if this is the first time for this backup and the above fcache() cache
    // instantiation correctly cached it, recache() will do nothing.
    fcache.recache(currentDir);

    // we can pull those out to display
    auto fcacheCurrent = fcache.getBackupByDir(currentDir);
    auto backupSize = GLOBALS.useBlocks ? fcacheCurrent->second.ds.sizeInBlocks : fcacheCurrent->second.ds.sizeInBytes;
    auto backupSaved = GLOBALS.useBlocks ? fcacheCurrent->second.ds.savedInBlocks : fcacheCurrent->second.ds.savedInBytes;

    // and only need to update the remaining fields
    fcacheCurrent->second.duration = backupTime.seconds();
    fcacheCurrent->second.finishTime = time(NULL);
    fcacheCurrent->second.modifiedFiles = filesModified - unmodDirs;
    fcacheCurrent->second.unchangedFiles = filesHardLinked;
    fcacheCurrent->second.dirs = unmodDirs;
    fcacheCurrent->second.slinks = filesSymLinked + receivedSymLinks;
    
    string message = "backup completed to " + currentDir + " in " + backupTime.elapsed() + "\n\t\t(total: " +
        to_string(fileTotal) + ", modified: " + to_string(filesModified - unmodDirs) + ", unchanged: " + to_string(filesHardLinked) + ", dirs: " + 
        to_string(unmodDirs) + ", symlinks: " + to_string(filesSymLinked + receivedSymLinks) + 
        (linkErrors ? ", linkErrors: " + to_string(linkErrors) : "") + 
        ", size: " + approximate(backupSize) + ", usage: " + approximate(backupSize - backupSaved) + ")";
    log(config.ifTitle() + " " + message);
    NOTQUIET && cout << "\t• " << config.ifTitle() << " " << message << endl;

    notify(config, "\t• " + message + "\n", true);

    // silently cleanup any old faub caches for backups that are no longer around
    FaubEntry("");  
}


/*
 * fc_scanToServer()
 * Scan a filesystem, sending the filenames and their associated mtime's back
 * to the remote server. This is the client's side of phase 1.
 */
void fc_scanToServer(string directory, tcpSocket& server) {
    DIR *c_dir;
    struct dirent *c_dirEntry;

    if ((c_dir = opendir(directory.c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {

            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
               continue;

            string fullFilename = directory + "/" + string(c_dirEntry->d_name);

            fileTransport fTrans(server);
            if (!fTrans.statFile(fullFilename)) {
                fTrans.sendStatInfo();
                DEBUG(D_netproto) DFMT("client provided stats on " << fullFilename);

                if (fTrans.isDir())
                    fc_scanToServer(fullFilename, server);
            }
        }

        closedir(c_dir);
    }
}

/*
 * fc_sendFilesToServer()
 * Receive a list of files from the server (client side of phase 2) and
 * send each file back to the server (client side of phase 3).
 */
void fc_sendFilesToServer(tcpSocket& server) {
    vector<string> neededFiles;

    while (1) {
        string filename = server.readTo(NET_DELIM);

        if (filename == NET_OVER)
            break;

        DEBUG(D_netproto) DFMT("client received request for " << filename);
        neededFiles.insert(neededFiles.end(), filename);
    }

    DEBUG(D_netproto) DFMT("client received requests for " << to_string(neededFiles.size()) << " file(s)");

    for (auto &file: neededFiles) {
        DEBUG(D_netproto) DFMT("client sending " << file << " to server");
        fileTransport fTrans(server);
        fTrans.statFile(file);
        fTrans.sendFullContents();
    }
}


void fc_mainEngine(vector<string> paths) {
    try {
        tcpSocket server(1, 60); // setup socket library to use stdout
        server.setReadFd(0); // and stdin

        DEBUG(D_faub) DFMT("faub client starting with " << paths.size() << " request(s)");

        for (auto it = paths.begin(); it != paths.end(); ++it) {
            log("faub client request for path: " + *it);

            server.write(string(*it + NET_DELIM).c_str());

            fc_scanToServer(*it, server);

            server.write(NET_OVER_DELIM);
            fc_sendFilesToServer(server);

            long end = (it+1) != paths.end();
            server.write(end);
        }
        
        DEBUG(D_netproto) DFMT("client complete.");
    }
    catch (string s) {
        cerr << "faub client caught internal exception: " << s << endl;
        log("error: faub client caught internal exception: " + s);
    }
    catch (...) {
        cerr << "faub client caught unknown exception" << endl;
        log("error: faub client caught unknown exception");
    }

    exit(1);
}


