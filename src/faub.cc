
#include "dirent.h"
#include "sys/stat.h"

#include "faub.h"
#include "PipeExec.h"
#include "debug.h"
#include "globals.h"

tuple<string, time_t> mostRecentBackupDirInternal(string backupDir);

string mostRecentBackupDir(string backupDir) {
    auto [fname, fmtime] = mostRecentBackupDirInternal(backupDir);
    return fname;
}


tuple<string, time_t> mostRecentBackupDirInternal(string backupDir) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    struct stat statData;

    string recentName;
    time_t recentTime = 0;

    Pcre matchSpec("-\\d{8}($|-\\d{2}:)");
    if (matchSpec.search(backupDir)) {

        if (!stat(backupDir.c_str(), &statData)) {
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
                    auto [fname, fmtime] = mostRecentBackupDirInternal(fullFilename);
                    if (fmtime > recentTime) {
                        recentTime = fmtime;
                        recentName = fname;
                    }
                }
                else
                    if (statData.st_mtime > recentTime) {
                        recentTime = statData.st_mtime;
                        recentName = fullFilename;
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
    strftime(buffer, sizeof(buffer), incTime ? "%Y/%m/%d" : "%Y/%m", localtime(&rawTime));
    string subDir = buffer;

    strftime(buffer, sizeof(buffer), incTime ? "-%Y%m%d-%H:%M:%S" : "-%Y%m%d", localtime(&rawTime));
    string filename = buffer;

    string fullPath = slashConcat(config.settings[sDirectory].value, subDir, safeFilename(config.settings[sTitle].value) + filename) + "/";

    return fullPath;
}


void fs_startServer(BackupConfig& config) {
    PipeExec faub(config.settings[sFaub].value);
    DEBUG(D_faub) DFMT("executing: \"" << config.settings[sFaub].value << "\"");
    faub.execute("faub", false, false, true);
    fs_serverProcessing(faub, config, mostRecentBackupDir(config.settings[sDirectory].value), newBackupDir(config));
}


void fs_serverProcessing(PipeExec& client, BackupConfig& config, string prevDir, string currentDir) {
    string remoteFilename;
    string localPrevFilename;
    string localCurFilename;
    struct stat statData;
    ssize_t fileTotal = 0;
    ssize_t fileModified = 0;
    ssize_t fileLinked = 0;
    ssize_t unmodDirs = 0;

    // note start time
    timer backupTime;
    backupTime.start();

    log(config.ifTitle() + " starting backup to " + currentDir);
    string screenMessage = config.ifTitle() + " backing up to " + currentDir + "... ";
    string backspaces = string(screenMessage.length(), '\b');
    string blankspaces = string(screenMessage.length() , ' ');
    NOTQUIET && ANIMATE && cout << screenMessage << flush;

    DEBUG(D_faub) DFMT("faub server ready to receive");
    DEBUG(D_faub) DFMT("current: " << currentDir);
    DEBUG(D_faub) DFMT("previous: " << prevDir);

    do {
        vector<string> neededFiles;
        map<string,string> linkList;

        /*
         * phase 1 - get list of filenames and mtimes from client
         * and see if the remote file is different from what we
         * have locally in the most recent backup.
        */

        while (1) {
            remoteFilename = client.readTo(NET_DELIM);
            DEBUG(D_faub) DFMT("server received filename " << remoteFilename);

            if (remoteFilename == NET_OVER) {
                break;
            }

            ++fileTotal;
            long mtime = client.readProc();
            long mode  = client.readProc();

            localPrevFilename = slashConcat(prevDir, remoteFilename);
            localCurFilename = slashConcat(currentDir, remoteFilename);

            /*
             * if the remote file and mtime match with the copy in our most recent
             * local backup then we want to hard link the current local backup's copy to
             * that previous backup version. if they don't match or the previous backup's
             * copy doesn't exist, add the file to the list of ones we need to get
             * from the remote end. For ones we want to link, add them to a list to do
             * at the end.  We can't do them now because the subdirectories they live in
             * may not have come over yet.
            */
            if (prevDir.length() && 
                    !stat(localPrevFilename.c_str(), &statData) && statData.st_mtime == mtime) {

                if (!S_ISDIR(mode))
                    linkList.insert(linkList.end(), pair<string, string>(localPrevFilename, localCurFilename));
                else {
                    /* 
                     * directories are special cases because we don't want to link them so
                     * we always need them sent over. unmodDirs is used just to keep the
                     * stats clean at the end.
                     */
                    neededFiles.insert(neededFiles.end(), remoteFilename);
                    ++unmodDirs;
                }
            }
            else
                neededFiles.insert(neededFiles.end(), remoteFilename);
        }

        DEBUG(D_faub) DFMT("server phase 1 complete; total:" << fileTotal << ", need:" << neededFiles.size() 
                << ", willLink:" << linkList.size());

        /*
         * phase 2 - send the client the list of files that we need full copies of
         * because they've changed or are missing from the previous backup
        */
        for (auto &file: neededFiles) {
            DEBUG(D_faub) DFMT("server requesting " << file);
            client.writeProc(string(file + NET_DELIM).c_str());
        }
        client.writeProc(NET_OVER_DELIM);

        DEBUG(D_faub) DFMT("server phase 2 complete; told client we need " << neededFiles.size() << " file(s)");

        /*
         * phase 3 - receive full copies of the files we've requested. they come over
         * in the order we've requested in the format of 8 bytes for 'size' and then
         * the 'size' number of bytes of data.
        */
        bool incTime = str2bool(config.settings[sIncTime].value);
        for (auto &file: neededFiles) {
            DEBUG(D_faub) DFMT("server waiting for " << file);
            client.readToFile(slashConcat(currentDir, file), !incTime);
        }

        DEBUG(D_faub) DFMT("server phase 3 complete; received " << neededFiles.size() << " files from client");

        /*
         * phase 4 - create the links for everything that matches the previous backup.
        */
        unsigned int linkErrors = 0;
        for (auto &links: linkList) {
            mkbasedirs(links.second);

            // when Time isn't included we're potentially overwriting an existing backup. let's pre-delete
            // so we don't get an error.
            if (!incTime)
                unlink(links.second.c_str());

            if (link(links.first.c_str(), links.second.c_str()) < 0) {
                ++linkErrors;
                SCREENERR("error: unable to link " << links.second << " to " << links.first << " - " << strerror(errno));
                log("error: unable to link " + links.second + " to " + links.first + " - " + strerror(errno));
            }
        }
        DEBUG(D_faub) DFMT("server phase 4 complete; created " << (linkList.size() - linkErrors)  << 
                " links to previously backed up files" << (linkErrors ? string(" (" + to_string(linkErrors) + " error(s))") : ""));

        fileModified += neededFiles.size();
        fileLinked += linkList.size();

    } while (client.readProc());
    
    // note finish time
    backupTime.stop();
    NOTQUIET && ANIMATE && cout << backspaces << blankspaces << backspaces << flush;

    string message = "backup completed to " + currentDir + " in " + backupTime.elapsed() + "\n\t\t(total files: " +
        to_string(fileTotal) + ", modified: " + to_string(fileModified - unmodDirs) + ", linked: " + to_string(fileLinked) + ")";
    log(config.ifTitle() + " " + message);
    NOTQUIET && cout << "\t• " << config.ifTitle() << " " << message << endl;
}


/*
 * fc_scanToServer()
 * Scan a filesystem, sending the filenames and their associated mtime's back
 * to the remote server. This is the client's side of phase 1.
 */
void fc_scanToServer(string directory, tcpSocket& server) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    struct stat statData;

    if ((c_dir = opendir(directory.c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {

            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
               continue;

            string fullFilename = directory + "/" + string(c_dirEntry->d_name);

            fileTransport fTrans(server);
            if (!fTrans.statFile(fullFilename)) {
                fTrans.sendStatInfo();

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
        DEBUG(D_faub) DFMT("client received request for " << filename);

        if (filename == NET_OVER)
            break;

        neededFiles.insert(neededFiles.end(), filename);
    }

    DEBUG(D_faub) DFMT("client received requests for " << to_string(neededFiles.size()) << " file(s)");

    for (auto &file: neededFiles) {
        DEBUG(D_faub) DFMT("client sending " << file << " to server");
        fileTransport fTrans(server);
        fTrans.statFile(file);
        fTrans.sendFullContents();
    }
}


void fc_mainEngine(vector<string> paths) {
    try {
        tcpSocket server(1); // setup socket library to use stdout
        server.setReadFd(0); // and stdin

        for (auto it = paths.begin(); it != paths.end(); ++it) {
            fc_scanToServer(*it, server);
            server.write(NET_OVER_DELIM);
            fc_sendFilesToServer(server);

            long end = (it+1) != paths.end();
            server.write(end);
        }
    }
    catch (string s) {
        cerr << "faub client caught internal exception: " << s << endl;
    }

    exit(1);
}

