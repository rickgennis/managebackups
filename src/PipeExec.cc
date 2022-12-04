#include <string>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <vector>
#include <utime.h>
#include "pcre++.h"
#include "util_generic.h"
#include "colors.h"
#include "globals.h"
#include "PipeExec.h"
#include "debug.h"


#define READ_END STDIN_FILENO
#define WRITE_END STDOUT_FILENO

#define DUP2(x,y) while (dup2(x,y) < 0 && errno == EINTR)

using namespace std;
using namespace pcrepp;



bool operator!=(const struct ProcDetail& A, const struct ProcDetail& B) {
    return !(A.command == B.command && A.childPID == B.childPID);
}

bool operator==(const struct ProcDetail& A, const struct ProcDetail& B) {
    return !(A != B);
}
 

string firstAvailIDForDir(string dir);


PipeExec::PipeExec(string fullCommand) {
    errorDir = "";
    origCommand = fullCommand;
    bypassDestructor = false;
    char *data;

    data = (char*)malloc(fullCommand.length() + 1);
    strcpy(data, fullCommand.c_str());
    char *p = strtok(data, "|");

    numProcs = 0;
    while (p) {
        ++numProcs;
        procs.insert(procs.end(), procDetail(trimSpace(p)));
        p = strtok(NULL, "|");
    }

    free(data);
}


PipeExec::~PipeExec() {
    if (!bypassDestructor) {
        closeAll();

        while (numProcs--)
            waitpid(-1, NULL, WNOHANG);

        if (!dontCleanup) {
            log("note: destructor calling flushErrors()");
            flushErrors();
        }
    }
}


void PipeExec::flushErrors() {
    if (errorDir.length()) {
        log("flushErrors(" + errorDir + "): " + origCommand);
        rmrfdir(errorDir);
    }
}


int PipeExec::closeAll() {
    if (procs.size() > 0) {
        close(procs[0].fd[READ_END]);
        int a = close(procs[0].fd[WRITE_END]);
        int b = close(procs[0].readfd[READ_END]);
        return(!(a == 0 && b == 0));
    }

    return 0;
}


int PipeExec::closeRead() {
    if (procs.size() > 0)
        return close(procs[0].fd[READ_END]);

    return 0;
}


int PipeExec::closeWrite() {
    if (procs.size() > 0)
        return close(procs[0].fd[WRITE_END]);

    return 0;
}


string ProcDetail::fdDetail() {
    return (to_string(fd[READ_END]) + (fcntl(fd[READ_END], F_GETFD) != -1 ? "o" : "c") + ", " + 
            to_string(fd[WRITE_END]) + (fcntl(fd[WRITE_END], F_GETFD) != -1 ? "o" : "c"));
}


void PipeExec::dump() {
    for (auto c = procs.begin() ; c != procs.end(); ++c) 
        cerr << "DUMP: [" << trimSpace(c->command) << "] " << c->fdDetail() << endl;;

}


int PipeExec::execute(string procName, bool leaveFinalOutput, bool noDestruct, bool noErrToDisk, bool noTmpCleanup) {
    bypassDestructor = noDestruct;
    dontCleanup = noTmpCleanup;
    procs.insert(procs.begin(), procDetail("head"));

    errorDir = string(TMP_OUTPUT_DIR) + "/" + (procName.length() ? safeFilename(procName) : "pid_" + to_string(getpid())) + "/";
    mkdirp(errorDir);
    string commandID = firstAvailIDForDir(errorDir);

    DEBUG(D_exec) DFMT("preparing full command [" << origCommand << "]");

    int commandIdx = -1;
    for (auto proc_it = procs.begin(); proc_it != procs.end(); ++proc_it) {
        ++commandIdx;
        string commandPrefix = proc_it->command.substr(0, proc_it->command.find(" "));
        string stderrFname = errorDir + commandID + ":" + to_string(commandIdx) + "." + safeFilename(commandPrefix) + ".stderr";

        // last loop
        if (*proc_it == procs[procs.size() - 1]) {
            DEBUG(D_exec) DFMT("executing final command [" << proc_it->command << "]");

            // redirect stderr to a file
            if (!noErrToDisk) {
                int errorFd = open(stderrFname.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                if (errorFd > 0) 
                    DUP2(errorFd, 2);
                else { 
                    string msg = "warning: unable to redirect STDERR of subprocess to " + stderrFname + " (A: " + strerror(errno) + ")";
                    SCREENERR("\n" << msg);
                    log(msg);
                }
            }

            // dup and close remaining fds
            auto back_it = proc_it - 1;
            DUP2(back_it->fd[READ_END], READ_END);
            close(procs[0].fd[WRITE_END]);
            close(procs[0].readfd[READ_END]);

            if (!leaveFinalOutput)
                DUP2(procs[0].readfd[WRITE_END], 1);

            // exec the last process
            varexec(proc_it->command);
        }
        else  {
            if (*proc_it == procs[0]) {
                pipe(proc_it->readfd);
            }

            // create the pipe & fork
            pipe(proc_it->fd);
            if ((proc_it->childPID = fork())) {

                // PARENT - all subsequent parents
                if (*proc_it != procs[0]) {
                    DEBUG(D_exec) DFMT("executing mid command [" << proc_it->command << "] with pipes " << proc_it->fd[0] << " & " << proc_it->fd[1]);

                    // redirect stderr to a file
                    if (!noErrToDisk) {
                        int errorFd = open(stderrFname.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                        if (errorFd > 0) 
                            DUP2(errorFd, 2);
                        else {
                            string msg = "warning: unable to redirect STDERR of subprocess to " + stderrFname + " (B: " + strerror(errno) + ")";
                            SCREENERR("\n" << msg);
                            log(msg);
                        }
                    }

                    // close fds from two procs back
                    if (procs.size() > 2 && *proc_it != procs[1]) {
                        auto back2_it = proc_it - 2;
                        close(back2_it->fd[0]);
                        close(back2_it->fd[1]);
                    }

                    // dup and close current and x - 1 fds
                    auto back_it = proc_it - 1;
                    close(proc_it->fd[READ_END]);
                    close(back_it->fd[WRITE_END]);
                    DUP2(back_it->fd[READ_END], READ_END);
                    DUP2(proc_it->fd[WRITE_END], WRITE_END);

                    // exec middle procs
                    varexec(proc_it->command);
                }

                // PARENT - first parent
                close(proc_it->fd[READ_END]);
                close(proc_it->readfd[WRITE_END]);
                if (procName.length())
                    errorDir = "";  // reset errorDir so that we don't clean up stderr files for a named proc
                return(proc_it->childPID);
            }
        }
        
        // CHILD
        if (*proc_it != procs[0]) {
            close(proc_it->fd[WRITE_END]);
            DUP2(proc_it->fd[READ_END], READ_END);
        }
    }

    exit(1);  // never get here
}


bool PipeExec::execute2file(string toFile, string procName) {
    int outFile;
    int bytesRead;
    int bytesWritten;
    int pos;
    bool success = false;
    char data[16 * 1024];

    DEBUG(D_exec) DFMT("toFile=" << toFile << "; procName=" << procName);

    if ((outFile = open(toFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR)) > 0) {
        execute(procName, false, false, false, true);

        while ((bytesRead = readProc(&data, sizeof(data)))) {
            pos = 0;
            while (((bytesWritten = write(outFile, data+pos, bytesRead)) < bytesRead) && (errno == EINTR)) {
                pos += bytesRead;
            }
        }

        close(outFile);
        close(procs[0].readfd[READ_END]);
        success = true;
    }
    else
        log("unable to write to " + toFile + ": " + strerror(errno));

    while (wait(NULL) > 0);

    return success;
}


void PipeExec::readAndTrash() {
    int bytesRead;
    char buffer[1024*8];

    while ((bytesRead = readProc(&buffer, sizeof(buffer))));
        // throw away bytes read until fd is closed on the other end
}


bool PipeExec::readAndMatch(string matchStr) {
    int bytesRead;
    char buffer[1024*2+1];
    bool found = false;

    while ((bytesRead = readProc(&buffer, sizeof(buffer) - 1))) {
        buffer[sizeof(buffer) - 1] = 0;
        if (strstr(buffer, matchStr.c_str()) != NULL)
            found = true;
        // keep reading the rest of the content to let the remote process finish
    }

    return found;
}


ssize_t PipeExec::readProc(void *buf, size_t count) {
    return read(procs[0].readfd[READ_END], buf, count); 
}


ssize_t PipeExec::writeProc(const void *buf, size_t count) {
    ssize_t bytesWritten;
    ssize_t totalBytesWritten = 0;

    while (count && ((bytesWritten = write(procs[0].fd[WRITE_END], buf, count)) > 0)) {
        count -= bytesWritten;
        totalBytesWritten += bytesWritten;
    }

    return totalBytesWritten;
}


ssize_t PipeExec::writeProc(const char *data) {
    return writeProc(data, strlen(data));
}


ssize_t PipeExec::writeProc(long data) {
    long netLong = htonl(data);
    return writeProc(&netLong, 8);
}


long PipeExec::readProc() {
    long data;

    string tempData;
    int pos = strBuf.length();
    if (pos) {
        int bytes = pos > 8 ? 8 : pos;
        tempData = strBuf.substr(0, bytes);
        strBuf.erase(0, bytes);
        log("PipeExec::readProc(8): pos = " + to_string(pos) + ", tempData = [" + tempData + "]");
        memcpy((char*)&data, tempData.c_str(), bytes);
    }
    log("PipeExec::readProc(8) mid phase");

    while (pos < 8) {
        log("PipeExec::readProc(8) reading from socket");
        pos += readProc((char*)&data + pos, 8 - pos);
    }

    long temp = ntohl(data);
    log("PipeExec::readProc(8) read " + to_string(temp));
    return temp;
}


string PipeExec::readTo(string delimiter) {
    while (1) {
        if (strBuf.length()) {
            size_t index;

            if ((index = strBuf.find(delimiter)) != string ::npos) {
                string result = strBuf.substr(0, index);
                strBuf.erase(0, index + delimiter.length());
                return result;
            }
        } 

        log("PipeExec::readTo() reading " + to_string(sizeof(rawBuf)));
        size_t bytes = readProc(rawBuf, sizeof(rawBuf));
        string tempStr(rawBuf, bytes);
        strBuf += tempStr;
        log("PipeExec::readTo() read " + tempStr);
    }
}


int PipeExec::getReadFd() {
    return procs[0].readfd[READ_END];
}


int PipeExec::getWriteFd() {
    return procs[0].fd[WRITE_END];
}


string PipeExec::statefulReadAndMatchRegex(string regex, int buffSize) {
    char buffer[buffSize];
    Pcre reg(regex);
    int bytesRead;

    while ((bytesRead = readProc(buffer, sizeof(buffer)))) {
        string tempStateBuffer(buffer, bytesRead);
        stateBuffer += tempStateBuffer;

        if (reg.search(stateBuffer) && reg.matches()) {
            auto pos = reg.get_match_end(0);
            string match = reg.get_match(0);
             
            stateBuffer.erase(0, pos);
            return match;
        }
    }

    return ""; 
}



string PipeExec::errorOutput() {
    return catdir(errorDir);
}


string firstAvailIDForDir(string dir) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    string lastID = "";

    if ((c_dir = opendir(dir.c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                continue;

            auto filename = string(c_dirEntry->d_name);
            auto delimit = filename.find(":");

            if (delimit != string::npos) {
                auto id = filename.substr(0, delimit);

                if (lastID < id)
                    lastID = id;
            }
        }

        closedir(c_dir);
    }

    if (!lastID.length())
        return "A";

    char lastLetter = lastID.back();
    if (lastLetter == 'Z')
        return(lastID + "A");

    int i = lastLetter;
    char c = (char)(i+1);
    char result[100];
    snprintf(result, sizeof(result), "%s%c", lastID.substr(0, lastID.length() - 1).c_str(), c);
    return(result);

}


void PipeExec::readToFile(string filename) {
    long uid = readProc();
    long gid = readProc();
    long mode = readProc();
    long mtime = readProc();

    //cout << "server: receive " << filename << ", mode:" << mode << ", uid: " << uid << ", gid: " << gid << ", mtime: " << mtime << endl;

    // handle directories that are specifically sent
    if (S_ISDIR(mode)) {
        mkdirp(filename);
        chmod(filename.c_str(), mode);
        chown(filename.c_str(), uid, gid);

        struct utimbuf timeBuf;
        timeBuf.actime = timeBuf.modtime = mtime;
        utime(filename.c_str(), &timeBuf);

        //cout << "server: created directory " << filename << endl;
        return;
    }

    // handle directories that are inherent in the filename
    string dirName = filename.substr(0, filename.find_last_of("/"));
    //cout << "making " << dirName << ": " << mkdirp(dirName);  // need a mode, gid and uid here

    if (S_ISLNK(mode)) {
        char target[1025];
        long bytes = readProc();
        readProc(target, bytes);
        target[bytes] = 0;
        if (symlink(target, filename.c_str())) {
            cerr << "unable to create symlink (" << filename << "): ";
            perror("");
            return;
        }
        chmod(filename.c_str(), mode);
        chown(filename.c_str(), uid, gid);

        //cout << "server: created symlink " << filename << " (-> " << target << ")" << endl;
        return;
    }

    // handle files
    auto bytesRemaining = readProc();
    //cout << "server: receiving " << filename << " as " << bytesRemaining << " bytes" << endl;

    FILE *dataf;
    auto bufSize = sizeof(rawBuf);
    dataf = fopen(filename.c_str(), "wb");

    /*
     * To maintain the network protocol with the client we have to read 'bytesRemaining' bytes
     * even if our fopen() failed and we can't save them to the local disk. That way all the
     * other read()s in this network connection still line up and subsequent files may transfer
     * even if there was an issue with this one.
     */

    //cout << "\tserver: created " << filename << endl;
    while (bytesRemaining) {
        size_t readSize = bytesRemaining < bufSize ? bytesRemaining : bufSize;
        size_t bytesRead = readProc(rawBuf, readSize);
        bytesRemaining -= bytesRead;

        if (dataf != NULL) {
            if (fwrite(rawBuf, 1, bytesRead, dataf) < bytesRead) {
                perror(filename.c_str());
                fclose(dataf);
                break;
            }
        }
    }

    if (dataf != NULL) {
        fclose(dataf);
        chown(filename.c_str(), uid, gid);
        chmod(filename.c_str(), mode);

        struct utimbuf timeBuf;
        timeBuf.actime = timeBuf.modtime = mtime;
        utime(filename.c_str(), &timeBuf);
    }

    //cout << "server: completed " << filename << endl;
}



