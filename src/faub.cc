
#include "dirent.h"
#include "sys/stat.h"

#include "faub.h"
#include "PipeExec.h"
#include "globals.h"


string mostRecentBackupDir(string backupDir) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    struct stat statData;

    string recentName;
    time_t recentTime = 0;
    if ((c_dir = opendir(backupDir.c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {

            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
               continue;

            string fullFilename = backupDir + "/" + string(c_dirEntry->d_name);
            if (!stat(fullFilename.c_str(), &statData)) {
                if (statData.st_mtime > recentTime) {
                    recentTime = statData.st_mtime;
                    recentName = fullFilename;
                }
            }
        }

        closedir(c_dir);
    }

    return recentName;
}


string newBackupDir(string backupDir, string baseFilename) {
    time_t rawTime;
    struct tm *timeFields;
    char buffer[100];

    time(&rawTime);
    timeFields = localtime(&rawTime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d@%X", timeFields);

    return(backupDir + "/" + buffer);
}


void fs_startServer(vector<BackupConfig>& configs, string dir) {
    // fork main listening daemon
/*    if (fork())
        return;

    try {
        tcpSocket listeningServer(2029, 50);

        while (1) {
            tcpSocket client = listeningServer.accept(15);

            // fork a daemon for this connection
            if (!fork()) {
                fs_serverProcessing(client, configs, mostRecentBackupDir(dir), newBackupDir(dir));
                exit(0);
            }
        }
    }
    catch (string s) {
        cerr << s << endl;
    }
*/
    
    cerr << "server: executing faub" << endl;
    PipeExec faub("/usr/bin/ssh fw /usr/bin/managebackups --directory /tmp --faubclient");
    faub.execute("faubcall", false, false, true);
    cerr << "server: setting up client socket" << endl;
    //tcpSocket client(faub.getWriteFd());
    //client.setReadFd(faub.getReadFd());
    cerr << "server: processing socket" << endl;
    fs_serverProcessing(faub, configs, mostRecentBackupDir(dir), newBackupDir(dir, "firewall"));
    cerr << "server: done" << endl;
    exit(0);
}


void fs_serverProcessing(PipeExec& client, vector<BackupConfig>& configs, string prevDir, string currentDir) {
    string remoteFilename;
    string localPrevFilename;
    string localCurFilename;
    struct stat statData;

    long clientId = client.readProc();
    if (clientId < 0 || clientId > configs.size()) {
        SCREENERR("Invalid session id (" << clientId << ").");
        exit(5);
    }

    do {
        vector<string> neededFiles;
        map<string,string> linkList;

        /*
         * phase 1 - get list of filenames and mtimes from client
         * and see if the remote file is different from what we
         * have locally in the most recent backup.
        */
        unsigned int fileCount = 0;
        unsigned int allFiles = 0;

        while (1) {
            remoteFilename = client.readTo(NET_DELIM);
            log("server: received filename [" + remoteFilename + "]");
            log("STATUS0");

            if (remoteFilename == NET_OVER) {
                break;
            }

            log("STATUS1");

            ++allFiles;
            long mtime = client.readProc();

            log("STATUS2");
            localPrevFilename = slashConcat(prevDir, remoteFilename);
            localCurFilename = slashConcat(currentDir, remoteFilename);
            log("STATUS3");

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
                    !stat(localPrevFilename.c_str(), &statData) &&
                    statData.st_mtime == mtime) {
                linkList.insert(linkList.end(), pair<string, string>(localPrevFilename, localCurFilename));
            }
            else {
                ++fileCount;
                neededFiles.insert(neededFiles.end(), remoteFilename);
                //cout << "\tserver: added needed file " << remoteFilename << endl;
            }
            log("STATUS4");
        }

        cerr << "server: phase 1 complete - examined " << allFiles << " files, need " << fileCount << " from client." << endl;

        /*
         * phase 2 - send the client the list of files that we need full copies of
         * because they've changed or are missing from the previous backup
        */
        for (auto &file: neededFiles) {
            log("server sending request for " + file);
            client.writeProc(string(file + NET_DELIM).c_str());
        }
        log("server telling client phase 2 complete");
        client.writeProc(NET_OVER_DELIM);
        //client.flush();

        cerr << "server: phase 2 complete, told client which files we need (" << neededFiles.size() << ")." << endl;
        log("server: phase 2 complete, told client which files we need (" + to_string(neededFiles.size()) + ").");

        /*
         * phase 3 - receive full copies of the files we've requested. they come over
         * in the order we've requested in the format of 8 bytes for 'size' and then
         * the 'size' number of bytes of data.
        */
        for (auto &file: neededFiles) {
            log("server waiting for " + file);
            cerr << "server waiting for " << file << endl;
            client.readToFile(slashConcat(currentDir, file));
        }
        log("server got all files - phase 3 complete");

        cerr << "server: phase 3 complete, received " << neededFiles.size() << " files from client." << endl;

        /*
         * phase 4 - create the links for everything that matches the previous backup.
        */
        for (auto &links: linkList) {
            mkbasedirs(links.second);
            link(links.first.c_str(), links.second.c_str());
        }
        cerr << "server: phase 4 complete, created " << linkList.size() << " links to previously backed up files.\n" << endl;

    } while (client.readProc());
    
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

            log("client sending file info for " + fullFilename);
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
        log("client got request for " + filename);
        cerr << "client got request for " << filename << endl;

        if (filename == NET_OVER)
            break;

        neededFiles.insert(neededFiles.end(), filename);
    }

    log("client got all file requests (" + to_string(neededFiles.size()) + ")");

    for (auto &file: neededFiles) {
        cerr << "sending " << file << " to server" << endl;
        fileTransport fTrans(server);
        fTrans.statFile(file);
        fTrans.sendFullContents();
    }

    cerr << "client: sent full file content for " << neededFiles.size() << " files" << endl;
}


void fc_mainEngine() {
    try {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stdin, NULL, _IONBF, 0);

        tcpSocket server(1);
        server.setReadFd(0);

        server.write(2);

        vector<string> dirs = { "/etc" };
        for (auto it = dirs.begin(); it != dirs.end(); ++it) {
            fc_scanToServer(*it, server);
            server.write(NET_OVER_DELIM);
            log("client finished sending initial file list");
            fc_sendFilesToServer(server);

            long end = (it+1) != dirs.end();
            server.write(end);
        }
    }
    catch (string s) {
        cerr << "caught internal exception: " << s << endl;
    }

    exit(1);
}

