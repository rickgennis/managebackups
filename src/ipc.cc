
#include <unistd.h>
#include <utime.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <pcre++.h>
#include <algorithm>
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
            throw MBException(string("error on select() of read - ") + strerror(errno));
        else {
            auto bytes = read(readFd, (char*)data + dataLen, count);
           
            if (bytes == -1) {
                ++ioErrors;
                
                if (ioErrors > 2)
                    throw MBException(string("multiple errors on network read - ") + strerror(errno));
            }
            else
                return bytes;
        }

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
        if (bytes == -1) {
            ++ioErrors;
            
            if (ioErrors > 2)
                throw MBException(string("multiple errors on network read - ") + strerror(errno));
        }
        else
            if (bytes) {
                string tempStr(rawBuf, bytes);
                strBuf += tempStr;
            }
            else
                return "";
    }
}


tuple<string, int, time_t> IPC_Base::ipcReadToFile(string filename, bool preDelete) {
    long uid = ipcRead();
    long gid = ipcRead();
    long mode = ipcRead();
    long mtime = ipcRead();
    string errorMsg;

    // handle directories that are specifically sent
    if (S_ISDIR(mode)) {
        if (mkdirp(filename, mode))
            errorMsg += (errorMsg.length() ? "\n" : "") + string("error: unable to mkdir ") + filename + errtext();
        
        if (chown(filename.c_str(), (int)uid, (int)gid))
            errorMsg += (errorMsg.length() ? "\n" : "") + string("error: unable to chown directory ") + filename + errtext();

        struct utimbuf timeBuf;
        timeBuf.actime = timeBuf.modtime = mtime;
        if (utime(filename.c_str(), &timeBuf))
            errorMsg += "error: unable to set utime() on " + filename + errtext();
        
        return {errorMsg, mode, mtime};
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
            return {("error: unable to create symlink " + filename + errtext()), -1, 0};

        if (lchown(filename.c_str(), (int)uid, (int)gid))
            return {("error: unable to chown symlink " + filename + errtext()), -1, 0};

        struct timeval tv[2];
        tv[0].tv_sec  = tv[1].tv_sec  = mtime;
        tv[0].tv_usec = tv[1].tv_usec = 0;
        lutimes(filename.c_str(), tv);

        return {"", mode, 0};
    }

    // handle directories that are inherent in the filename
    string dirName = filename.substr(0, filename.find_last_of("/"));
    if (mkdirp(dirName))
        return {("error: unable to mkdir " + filename + ": " + strerror(errno)), 0, 0};

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
        return {errorMsg, mode, mtime};
    }

    DEBUG(D_netproto) cerr << " [" << totalBytes << " bytes] can't write file" << flush;
    return {errorMsg, mode, mtime};
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

    if (!mylstat(filename, &statData) && statData.st_mode) {
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
        mylstat(filename, &statData);
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

    while (p) {
        procs.insert(procs.end(), procDetail(trimSpace(p)));
        p = strtok(NULL, "|");
    }

    free(data);
}


void PipeExec::pickupTheKids() {
    bool done = true;
    
    // if any previous child procs have been reaped, check them first in our global list.
    // loop through the list of child procs we're looking to reap
    for (auto procIt = procs.begin(); procIt != procs.end(); ++procIt) {
        
        // valid pids that are still waiting
        if (procIt->childPID && !procIt->reaped) {
            
            // is this pid on our previously reaped list?
            if (GLOBALS.reapedPids.find(procIt->childPID) != GLOBALS.reapedPids.end()) {
                GLOBALS.reapedPids.erase(procIt->childPID);
                procIt->reaped = true;
                DEBUG(D_exec) DFMT("pid " + to_string(GLOBALS.pid) + " successfully reaped child pid " + to_string(procIt->childPID) + "*");
            }
            else
                // if not then that's still a valid pid that's outstanding and we need to
                // wait() for it further below
                done = false;
        }
    }
    
    
    // in this loop 'done' is used to track whether there are any child pids that are not
    // yet reaped and therefore we need to continue waiting for
    while (!done) {
        
        // wait for a child pid
        auto pid = wait(NULL);
        
        if (pid > 0) {
            bool pidFound = false;
            done = true;
            
            // loop through the list of child pids we're waiting on. looping is better than
            // a find() because we not only want to locate the matching pid but we want to
            // know if there are any others ramaining in the list, meaning we need to wait()
            // some more.
            for (auto procIt = procs.begin(); procIt != procs.end(); ++procIt) {
                if (procIt->childPID == pid) {
                    procIt->reaped = pidFound = true;
                    DEBUG(D_exec) DFMT("pid " + to_string(GLOBALS.pid) + " successfully reaped child pid " + to_string(pid));
                    
                    // bail early if we've hit both conditions (a pid found & a pid not found)
                    if (!done)
                        break;
                }
                else
                    if (!procIt->reaped && procIt->childPID) {
                        done = false;

                        // bail early if we've hit both conditions (a pid found & a pid not found)
                        if (pidFound)
                            break;
                    }
            }

            // wait() may return info on a pid that's not part of our list, likely part of some
            // other PipeExec() instance. stash the pid in our global list which will get
            // checked by those other instances before they bother to call wait().
            if (!pidFound)
                GLOBALS.reapedPids.insert(GLOBALS.reapedPids.end(), pid);
        }
    }
}


PipeExec::~PipeExec() {
    if (!bypassDestructor) {
        closeAll();
        
        pickupTheKids();
        
        if (!dontCleanup) {
            flushErrors();
        }
    }
}


void PipeExec::flushErrors() {
    if (errorDir.length())
        rmrf(errorDir);
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
        string msg = "warning: unable to redirect STDERR of subprocess to " + filename + " (" + strerror(errno) + ")";
        SCREENERR("\n" << msg);
        log(msg);
    }
}


int PipeExec::execute(string procName, bool leaveFinalOutput, bool noDestruct, bool noErrToDisk, bool noTmpCleanup) {
    bypassDestructor = noDestruct;
    dontCleanup = noTmpCleanup;

    if (!procs.size())
        return 0;
    
    errorDir = string(TMP_OUTPUT_DIR) + "/" + (procName.length() ? safeFilename(procName) : "pid_" + to_string(getpid())) + "/";
    if (mkdirp(errorDir))
        showError("error: unable to mkdir " + errorDir + ": " + strerror(errno));

    string commandID = firstAvailIDForDir(errorDir);
    DEBUG(D_exec) DFMT(to_string(getpid()) + " preparing full command [" << origCommand << "]");

    /* Each pipe in the exec string denotes a separate proc.
     
       Given our design, reading and writing to the first proc in the chain is easier
       if we insert a dummy proc at the beginning of the chain, which we just use for
       file descriptors.  The dummy entry together with the real list of procs fall into
       3 possible categories:
     
       - The first proc: this is always the dummy entry. On this iteration of the loop
         it fork()s just the others but the parent (i.e. our original proc) sets up the
         reading and writing FDs and then immediately returns to our calling process.
     
       - The middle procs: Zero or more procs may fall into this category. For each of
         these we fork() and then in the parent exec() the command to create the proc.
         In the child we continue to the top of the loop and are considered the parent
         for the next proc -- fork() again...
     
       - The last proc: Last proc gets fork()ed by either the first or middle proc. The
         difference is the last proc does different FD clean up depending on whether it's
         going to leave its STDOUT as a default (such as someone executing 'less' and
         expecting it to spill onto the screen) or will it be redirected back to the
         output of the dummy proc at the beginning so our calling app can read from it.
     
         First, insert the dummy proc...
     */
    procs.insert(procs.begin(), procDetail("head"));

    // loop through the list of procs that we need to kick off
    for (auto procIt = procs.begin(); procIt != procs.end(); ++procIt) {
        string commandPrefix = procIt->command.substr(0, procIt->command.find(" "));
        string stderrFname = errorDir + commandID + ":" + to_string(distance(procs.begin(), procIt)) + "." + safeFilename(commandPrefix) + ".stderr";

        if (procIt != procs.end() - 1) {  // if not last proc
            if (procIt == procs.begin())
                pipe(procIt->readfd);  // extra pipe for our dummy entry to communicate with the calling app

            // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
            // Pipe & Fork
            // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
            pipe(procIt->writefd);
            if (((procIt+1)->childPID = fork())) {
                
                // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
                // Middle Procs:  PARENT (i.e. all subsequent
                // parents) - execute the command
                // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
                if (procIt != procs.begin()) {  // if not first proc
                    DEBUG(D_exec) DFMT(to_string(getpid()) + " executing mid command [" << procIt->command << "] with pipes " << procIt->writefd[0] << " & " << procIt->writefd[1]);

                    // redirect stderr to a file
                    if (!noErrToDisk)
                        redirectStdError(stderrFname);

                    // close fds from two procs back
                    if (procs.size() > 2 && *procIt != procs[1]) {
                        auto back2_it = procIt - 2;
                        close(back2_it->writefd[0]);
                        close(back2_it->writefd[1]);
                    }

                    // dup and close current and x - 1 fds
                    auto backIt = procIt - 1;
                    close(procIt->writefd[READ_END]);
                    close(backIt->writefd[WRITE_END]);
                    DUP2(backIt->writefd[READ_END], READ_END);
                    DUP2(procIt->writefd[WRITE_END], WRITE_END);

                    // exec middle procs
                    varexec(procIt->command);
                }

                // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
                // First Proc:  PARENT - the dummy proc that returns
                // FDs to the calling app
                // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
                close(procIt->writefd[READ_END]);
                close(procIt->readfd[WRITE_END]);
                if (procName.length())
                    errorDir = "";  // reset errorDir so that we don't clean up stderr files for a named proc

                readFd = procs[0].readfd[READ_END];
                writeFd = procs[0].writefd[WRITE_END];

                return((procIt+1)->childPID);
            }
        }
        else  {
            // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
            // Last Proc: PARENT - execute the last command
            // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
            DEBUG(D_exec) DFMT(to_string(getpid()) + " executing final command [" << procIt->command << "]");

            // redirect stderr to a file
            if (!noErrToDisk)
                redirectStdError(stderrFname);
        
            // dup and close remaining fds
            auto backIt = procIt - 1;
            DUP2(backIt->writefd[READ_END], READ_END);
            close(procs[0].writefd[WRITE_END]);
            close(procs[0].readfd[READ_END]);

            if (!leaveFinalOutput)
                DUP2(procs[0].readfd[WRITE_END], 1);

            // exec the last process
            varexec(procIt->command);
        }

        // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
        // CHILD - all children
        // It's confusing because a proc will come out of the
        // fork as a child and immediately hit this block,
        // only to then get back up to the top of the for() loop
        // and be considered a parent (the same PID) going
        // into the next fork iteration. Remember, it's not
        // a parent having a bunch of kids; it's multiple
        // generations where each child has the next child.
        // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
        if (*procIt != procs[0]) {
            close(procIt->writefd[WRITE_END]);
            DUP2(procIt->writefd[READ_END], READ_END);
        }
    }

    /* The original parent return()s to the calling app with FDs
       for communicating to the process list. All the children
       on down end in an exec() call to whatever command they're
       running.  So nothing ever gets to this purely decorative
       exit(). */
    exit(1);
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

    pickupTheKids();

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
