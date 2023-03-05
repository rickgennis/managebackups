
#include <unistd.h>
#include <utime.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <pcre++.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <dirent.h>


#include "ipc.h"
#include "util_generic.h"
#include "exception.h"
#include "debug.h"

#define READ_END STDIN_FILENO
#define WRITE_END STDOUT_FILENO

using namespace pcrepp;
using namespace std;

// helper function for debugging
string firstAvailIDForDir(string dir);


/********************************************************************
 *
 * IPC_Base
 *
 *******************************************************************/

ssize_t IPC_Base::ipcRead(void *data, size_t count) {
    auto bufLen = strBuf.length();
    size_t dataLen = 0;

    if (bufLen) {
        dataLen = bufLen > count ? count : bufLen;
        memcpy(data, strBuf.c_str(), dataLen);
        strBuf.erase(0, dataLen);

        if (count <= dataLen)
            return(dataLen);

        count -= dataLen;
    }

    int result = simpleSelect(readFd, 0, timeoutSecs);

    if (result == 0) {
        log("timeout on read()");
        throw MBException("timeout on read()");
    }
    else
        if (result == -1)
            log(string("error on select() of read: ") + strerror(errno));
        else
            return read(readFd, (char*)data + dataLen, count);

    return 0;
}


/* read 8-bytes and return it as a 64-bit int */
__int64_t IPC_Base::ipcRead() {   
    __int64_t data = 0;
    int bytes = 0;
    auto bufLen = strBuf.length();

    do {
        if (bufLen) {
            size_t dataLen = bufLen > 8 ? 8 : bufLen;
            memcpy(&data, strBuf.c_str(), dataLen);
            strBuf.erase(0, dataLen);

            if (dataLen == 8)
                break;  // skip ipcRead()

            bytes += dataLen;
        }

        while (bytes < 8)
            bytes += ipcRead((char*)&data + bytes, 8 - bytes);
    } while (false);

#if defined(__linux__)
    __int64_t temp = be64toh(data);
#else
    __int64_t temp = ntohll(data);
#endif

    return temp;
}


string IPC_Base::ipcReadTo(string delimiter) {
    while (1) {
        if (strBuf.length()) {
            size_t index;

            if ((index = strBuf.find(delimiter)) != string::npos) {
                string result = strBuf.substr(0, index);
                strBuf.erase(0, index + delimiter.length());
                return result;
            }
        }

        auto bytes = read(readFd, rawBuf, sizeof(rawBuf));

        if (bytes) {
            string tempStr(rawBuf, bytes);
            strBuf += tempStr;
        }
        else
            return "";
    }
}


tuple<string, int> IPC_Base::ipcReadToFile(string filename, bool preDelete) {
    long uid = ipcRead();
    long gid = ipcRead();
    long mode = ipcRead();
    long mtime = ipcRead();
    string errorMsg;

    // handle directories that are specifically sent
    if (S_ISDIR(mode)) {
        if (mkdirp(filename))
            errorMsg += (errorMsg.length() ? "\n" : "") + string("error: unable to mkdir ") + filename + ": " + strerror(errno);

        if (chmod(filename.c_str(), mode))
            errorMsg += (errorMsg.length() ? "\n" : "") + string("error: unable to chmod directory ") + filename + ": " + strerror(errno);

        if (chown(filename.c_str(), (int)uid, (int)gid))
            errorMsg += (errorMsg.length() ? "\n" : "") + string("error: unable to chown directory ") + filename + ": " + strerror(errno);

        struct utimbuf timeBuf;
        timeBuf.actime = timeBuf.modtime = mtime;
        utime(filename.c_str(), &timeBuf);

        return {errorMsg, mode};
    }

    // handle symlinks
    if (S_ISLNK(mode)) {
        char target[1025];
        __int64_t bytes = ipcRead();
        ipcRead(target, bytes);
        target[bytes] = 0;

        if (preDelete)
            unlink(filename.c_str());

        if (symlink(target, filename.c_str()))
            return {("error: unable to create symlink (" + filename + "): " + strerror(errno)), -1};

        if (lchown(filename.c_str(), (int)uid, (int)gid))
            return {("error: unable to chown symlink " + filename + ": " + strerror(errno)), -1};

        struct timeval tv[2];
        tv[0].tv_sec  = tv[1].tv_sec  = mtime;
        tv[0].tv_usec = tv[1].tv_usec = 0;
        lutimes(filename.c_str(), tv);

        return {"", mode};
    }

    // handle directories that are inherent in the filename
    string dirName = filename.substr(0, filename.find_last_of("/"));
    if (mkdirp(dirName))
        return {("error: unable to mkdir " + filename + ": " + strerror(errno)), 0};

    // handle files
    auto bytesRemaining = ipcRead();
    auto totalBytes = bytesRemaining;

    FILE *dataf;
    auto bufSize = sizeof(rawBuf);
    dataf = fopen(ue(filename).c_str(), "wb");

    /*
     * To maintain the network protocol with the client we have to read 'bytesRemaining' bytes
     * even if our fopen() failed and we can't save them to the local disk. That way all the
     * other read()s in this network connection still line up and subsequent files may transfer
     * even if there was an issue with this one.
     */

    bool errorLogged = false;
    while (bytesRemaining) {
        auto readSize = bytesRemaining < bufSize ? bytesRemaining : bufSize;
        auto bytesRead = ipcRead(rawBuf, readSize);
        bytesRemaining -= bytesRead;

        if (dataf != NULL && !errorLogged) {
            if (fwrite(rawBuf, 1, bytesRead, dataf) < bytesRead) {
                errorLogged = true;
                errorMsg += (errorMsg.length() ? "\n" : "") + string("error: unable to fwrite to ") + filename + ": " + strerror(errno);
                fclose(dataf);
            }
        }
    }

    if (dataf != NULL) {
        fclose(dataf);
        if (chown(filename.c_str(), (int)uid, (int)gid))
            errorMsg += (errorMsg.length() ? "\n" : "") + string("error: unable to chown file ") + filename + ": " + strerror(errno);

        if (chmod(filename.c_str(), mode))
            errorMsg += (errorMsg.length() ? "\n" : "") + string("error: unable to chmod file ") + filename + ": " + strerror(errno);

        struct utimbuf timeBuf;
        timeBuf.actime = timeBuf.modtime = mtime;
        utime(filename.c_str(), &timeBuf);
        DEBUG(D_netproto) cerr << " [" << totalBytes << " bytes]" << flush;
        return {errorMsg, mode};
    }

    DEBUG(D_netproto) cerr << " [" << totalBytes << " bytes] can't write file" << flush;
    return {errorMsg, mode};
}


void IPC_Base::readAndTrash() {
    size_t bytesRead;

    while ((bytesRead = ipcRead(&rawBuf, sizeof(rawBuf))));    
        // throw away bytes read until fd is closed on the other end
}


bool IPC_Base::readAndMatch(string matchStr) {
    ssize_t bytesRead;
    bool found = false;

    while ((bytesRead = ipcRead(&rawBuf, sizeof(rawBuf) - 1))) {
        rawBuf[sizeof(rawBuf) - 1] = 0;

        if (strstr(rawBuf, matchStr.c_str()) != NULL)
            found = true;

        // keep reading the rest of the content to let the remote process finish
    }

    return found;
}


string IPC_Base::statefulReadAndMatchRegex(string regex) {
    Pcre reg(regex);
    ssize_t bytesRead;
    string stateBuffer;

    while ((bytesRead = ipcRead(rawBuf, sizeof(rawBuf)))) {
        string tempStateBuffer(rawBuf, bytesRead);
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


ssize_t IPC_Base::ipcWrite(const void *data, size_t count) {
    ssize_t bytesWritten;
    ssize_t totalBytesWritten = 0;

    int result = simpleSelect(0, writeFd, timeoutSecs);

    if (result > 0)
        while (count && ((bytesWritten = write(writeFd, (char*)data + totalBytesWritten, count)) > 0)) {
            count -= bytesWritten;
            totalBytesWritten += bytesWritten;
        }
    else {
        if (result == 0) {
            log("timeout on write()");
            throw MBException("timeout on write()");
        }
        else
            log(string("error on select() of write: ") + strerror(errno));
    }

    return totalBytesWritten;
}


ssize_t IPC_Base::ipcWrite(const char *data) {
    return ipcWrite(data, strlen(data));
}


ssize_t IPC_Base::ipcWrite(__int64_t data) {
#if defined(__linux__)
    __int64_t netLong = htobe64(data);
#else
    __int64_t netLong = htonll(data);
#endif

    return ipcWrite(&netLong, 8);
}


void IPC_Base::ipcSendDirEntry(string filename) {
    struct stat statData;

    if (!lstat(filename.c_str(), &statData) && statData.st_mode) {
        ipcWrite(statData.st_uid);
        ipcWrite(statData.st_gid);
        ipcWrite(statData.st_mode);
        ipcWrite(statData.st_mtime);

        if (S_ISLNK(statData.st_mode)) {
            char target[1024];
            auto bytes = readlink(filename.c_str(), target, sizeof(target));
            ipcWrite(bytes);
            ipcWrite(target, bytes);
        }
        else
            if (!S_ISDIR(statData.st_mode)) {
                ipcSendRawFile(filename, statData.st_size);
            }
    }
    else
        ipcWrite((__int64_t)0);
}


void IPC_Base::ipcSendRawFile(string filename, __int64_t fileSize) {
    FILE *dataf;

    struct stat statData;
    if (!fileSize)
        lstat(filename.c_str(), &statData);
    else
        statData.st_size = fileSize;

    if ((dataf = fopen(ue(filename).c_str(), "rb")) != NULL) {
        ipcWrite(statData.st_size);

        ssize_t bytesRead;
        while ((bytesRead = fread(rawBuf, 1, sizeof(rawBuf), dataf)))
            ipcWrite(rawBuf, bytesRead);

        fclose(dataf);
    }
    else {
        ipcWrite((__int64_t)0);
        log("error: unable to read " + filename);
    }
}


void IPC_Base::ipcClose() {
    close(readFd);
    close(writeFd);
}


/********************************************************************
 *
 * TCP_Socket
 *
 *******************************************************************/

TCP_Socket::TCP_Socket(int port, int backlog, unsigned int timeout) : IPC_Base(0, 0, timeout) {
    if ((readFd = socket(AF_INET, SOCK_STREAM, 0)) > 0) {
        writeFd = readFd;
        int option = 1;

        if (!setsockopt(readFd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option))) {
            struct sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(port);

            if (::bind(readFd, (struct sockaddr*)&address, sizeof(address)) == 0) {
                if (!listen(readFd, backlog))
                    return;
                else {
                    readFd = writeFd = 0;
                    throw(MBException(string("listen(server): ") + strerror(errno)));
                }
            }
            else {
                readFd = writeFd = 0;
                throw(MBException(string("bind(server): ") + string(strerror(errno))));
            }
        }
        else {
            readFd = writeFd = 0;
            throw(MBException(string("setsockopt(server): ") + string(strerror(errno))));
        }
    }
    else {
        readFd = writeFd = 0;
        throw(MBException(string("socket(server): ") + string(strerror(errno))));
    }
}


TCP_Socket::TCP_Socket(string server, int port, unsigned int timeout) : IPC_Base(0, 0, timeout) {
    if ((readFd = socket(AF_INET, SOCK_STREAM, 0)) > 0) {
        writeFd = readFd;
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_port = htons(port);

        if (inet_pton(AF_INET, server.c_str(), &address.sin_addr) > 0) {
            if (!connect(readFd, (struct sockaddr*)&address, sizeof(address)))
                return;
            else {
                readFd = writeFd = 0;
                throw(MBException(string("connect(client): ") + string(strerror(errno))));
            }
        }
        else {
            readFd = writeFd = 0;
            throw(MBException(string("inet_pton(client): ") + string(strerror(errno))));
        }
    }
    else {
        readFd = writeFd = 0;
        throw(MBException(string("socket(client): ") + string(strerror(errno))));
    }
}


TCP_Socket::TCP_Socket(int fd, unsigned int timeout) : IPC_Base(fd, fd, timeout) {};


TCP_Socket TCP_Socket::accept() {
    if (!simpleSelect(readFd, 0, timeoutSecs)) {
        log("timeout on socket accept()");
        throw MBException("timeout on socket accept()");
    }

    int newFd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((newFd = ::accept(readFd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) > 0) {
        TCP_Socket client(newFd, timeoutSecs);
        return client;
    }

    throw MBException(string("accept(client): " ) + strerror(errno));
}


/********************************************************************
 *
 * PipeExec
 *
 *******************************************************************/

bool operator!=(const struct ProcDetail& A, const struct ProcDetail& B) {
    return !(A == B);
}


bool operator==(const struct ProcDetail& A, const struct ProcDetail& B) {
    return (A.command == B.command && A.childPID == B.childPID);
}


PipeExec::PipeExec(string command, unsigned int timeout) : IPC_Base(0, 0, timeout) {
    timeoutSecs = timeout;
    origCommand = command;
    bypassDestructor = false;
    errorDir = "";
    char *data;

    data = (char*)malloc(command.length() + 1);
    strcpy(data, command.c_str());
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

        //cerr << "pid " << getpid() << " has " << numProcs << " child proc(s)" << endl;
        while (numProcs--) {
            //cerr << "pid " << getpid() << " pre-wait" << endl;
            waitpid(-1, NULL, WNOHANG);
            //auto w = wait(NULL);
            //cerr << "pid " << getpid() << " returned " << w << endl;
        }

        
        if (!dontCleanup) {
            flushErrors();
        }
    }
}


void PipeExec::flushErrors() {
    if (errorDir.length()) {
        //cerr << "pid " << getpid() << " cleaning up " << errorDir << endl;
        rmrf(errorDir);
    }
}


int PipeExec::closeAll() {
    if (procs.size() > 0) {
        close(procs[0].writefd[READ_END]);
        int a = close(procs[0].writefd[WRITE_END]);
        int b = close(procs[0].readfd[READ_END]);
        return(!(a == 0 && b == 0));
    }

    return 0;
}


int PipeExec::closeRead() {
    if (procs.size() > 0)
        return close(procs[0].writefd[READ_END]);

    return 0;
}


int PipeExec::closeWrite() {
    if (procs.size() > 0)
        return close(procs[0].writefd[WRITE_END]);

    return 0;
}

void redirectStdError(string filename) {
    int errorFd = open(filename.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (errorFd > 0)
        DUP2(errorFd, 2);
    else {
        cerr << "pid " << getpid() << " (parent is " << getppid() << ") unable to write to " << filename << endl;
        string msg = "warning: unable to redirect STDERR of subprocess to " + filename + " (" + strerror(errno) + ")";
        SCREENERR("\n" << msg);
        log(msg);
    }
}


int PipeExec::execute(string procName, bool leaveFinalOutput, bool noDestruct, bool noErrToDisk, bool noTmpCleanup) {
    bypassDestructor = noDestruct;
    dontCleanup = noTmpCleanup;
    procs.insert(procs.begin(), procDetail("head"));

    errorDir = string(TMP_OUTPUT_DIR) + "/" + (procName.length() ? safeFilename(procName) : "pid_" + to_string(getpid())) + "/";
    if (mkdirp(errorDir))
        showError("error: unable to mkdir " + errorDir + ": " + strerror(errno));

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
            if (!noErrToDisk)
                redirectStdError(stderrFname);
        
            // dup and close remaining fds
            auto back_it = proc_it - 1;
            DUP2(back_it->writefd[READ_END], READ_END);
            close(procs[0].writefd[WRITE_END]);
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
            pipe(proc_it->writefd);
            if ((proc_it->childPID = fork())) {

                // PARENT - all subsequent parents
                if (*proc_it != procs[0]) {
                    DEBUG(D_exec) DFMT("executing mid command [" << proc_it->command << "] with pipes " << proc_it->writefd[0] << " & " << proc_it->writefd[1]);

                    // redirect stderr to a file
                    if (!noErrToDisk)
                        redirectStdError(stderrFname);

                    // close fds from two procs back
                    if (procs.size() > 2 && *proc_it != procs[1]) {
                        auto back2_it = proc_it - 2;
                        close(back2_it->writefd[0]);
                        close(back2_it->writefd[1]);
                    }

                    // dup and close current and x - 1 fds
                    auto back_it = proc_it - 1;
                    close(proc_it->writefd[READ_END]);
                    close(back_it->writefd[WRITE_END]);
                    DUP2(back_it->writefd[READ_END], READ_END);
                    DUP2(proc_it->writefd[WRITE_END], WRITE_END);

                    // exec middle procs
                    varexec(proc_it->command);
                }

                // PARENT - first parent
                close(proc_it->writefd[READ_END]);
                close(proc_it->readfd[WRITE_END]);
                if (procName.length())
                    errorDir = "";  // reset errorDir so that we don't clean up stderr files for a named proc

                readFd = procs[0].readfd[READ_END];
                writeFd = procs[0].writefd[WRITE_END];

                return(proc_it->childPID);
            }
        }

        // CHILD
        if (*proc_it != procs[0]) {
            close(proc_it->writefd[WRITE_END]);
            DUP2(proc_it->writefd[READ_END], READ_END);
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
    char data[64 * 1024];

    DEBUG(D_exec) DFMT("toFile=" << toFile << "; procName=" << procName);

    if ((outFile = open(toFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR)) > 0) {
        execute(procName, false, false, false, true);

        while ((bytesRead = (int)ipcRead(&data, sizeof(data)))) {
            pos = 0;
            while (((bytesWritten = (int)write(outFile, data+pos, bytesRead)) < bytesRead) && (errno == EINTR)) {
                pos += bytesRead;
            }
        }

        close(outFile);
        close(procs[0].readfd[READ_END]);
        success = true;
    }
    else
        log("unable to write to " + toFile + ": " + strerror(errno));

    while (wait(NULL) > 0)
        numProcs--;

    return success;
}


string PipeExec::errorOutput() {
    return catdir(errorDir);
}


string firstAvailIDForDir(string dir) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    string lastID = "";

    if ((c_dir = opendir(ue(dir).c_str())) != NULL) {
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
