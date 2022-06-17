#include <unistd.h>
#include <iostream>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "filesystem.h"

using namespace std;


/*
 * c_scanToServer()
 * Scan a filesystem, sending the filenames and their associated mtime's back
 * to the remote server. This is the client's side of phase 1.
 */
void c_scanToServer(string directory, tcpSocket& server) {
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
                    c_scanToServer(fullFilename, server);
            }
        }

        closedir(c_dir);
    }

}

/*
 * c_sendFilesToServer()
 * Receive a list of files from the server (client side of phase 2) and
 * send each file back to the server (client side of phase 3).
 */
void c_sendFilesToServer(tcpSocket& server) {
    vector<string> neededFiles;

    unsigned int fileCount = 0;
    while (1) {
        string filename = server.readTo(NET_DELIM);

        if (filename == NET_OVER)
            break;

        ++fileCount;
        neededFiles.insert(neededFiles.end(), filename);
    }

    for (auto &file: neededFiles) {
        fileTransport fTrans(server);
        fTrans.statFile(file);
        fTrans.sendFullContents();
    }

    cout << "client: sent full file content for " << fileCount << " files" << endl;
}

