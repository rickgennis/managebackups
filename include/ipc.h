
#ifndef IPC_H
#define IPC_H

#include <string>
#include <vector>
#include <tuple>

#define BUFFER_SIZE     (1024 * 64)
#define NET_DELIM       ";\n"
#define NET_OVER        "///;/"
#define NET_OVER_DELIM  string(string(NET_OVER) + string(NET_DELIM)).c_str()


using namespace std;


/********************************************************************
 * IPC_Base
 * This can be used for basic reads/writes across a file descriptor,
 * including existing pipes (stdin/stdout) where nothing is being
 * executed (forked). To add execution with auto pipe linking,
 * use PipeExec. To add socket initiation (listen/accept/connect),
 * use TCP_Socket. Both rely on IPC_Base for fundamental I/O.
 *******************************************************************/
class IPC_Base {
protected:
    int readFd;
    int writeFd;
    unsigned int timeoutSecs;
    string strBuf;
    char rawBuf[BUFFER_SIZE];
    int ioErrors;
    
public:
    /* structors */
    IPC_Base(int rFd, int wFd, unsigned int timeout = 120) : readFd(rFd), writeFd(wFd), timeoutSecs(timeout) { ioErrors = 0; };
    ~IPC_Base() { ipcClose(); }
    
    /* reads */
    ssize_t ipcRead(void *data, size_t count);
    __int64_t ipcRead();
    string ipcReadTo(string delimiter);
    tuple<string, int, time_t> ipcReadToFile(string filename, bool preDelete = false);
    void readAndTrash();
    bool readAndMatch(string matchStr);
    string statefulReadAndMatchRegex(string regex);
    
    /* writes */
    ssize_t ipcWrite(const void *data, size_t count);
    ssize_t ipcWrite(const char *data);
    ssize_t ipcWrite(__int64_t data);
    void ipcSendDirEntry(string filename);
    void ipcSendRawFile(string filename, __int64_t fileSize = 0);
    
    /* administration */
    void ipcClose();
};


/********************************************************************
 * TCP_Socket
 * This adds basic socket operations on top of IPC_Base.
 *******************************************************************/
class TCP_Socket : public IPC_Base {
public:
    /* structors */
    TCP_Socket(int port, int backlog, unsigned int timeout);   // listen as server
    TCP_Socket(string server, int port, unsigned int timeout); // connect as client
    TCP_Socket(int fd, unsigned int timeout);                  // just set fds
    ~TCP_Socket() { ipcClose(); }
    
    /* administration */
    TCP_Socket accept();
};


/********************************************************************
 ProcDetail is used internally by PipeExec for tracking processes.
 You never need to instantiate anything of this type.
 *******************************************************************/
typedef struct ProcDetail {
    int writefd[2];
    int readfd[2];
    string command;
    unsigned int childPID;
    bool reaped;
    
    ProcDetail(string cmd) { command = cmd; writefd[0] = writefd[1] = readfd[0] = readfd[1] = childPID = reaped = 0; }
    string fdDetail();
    
    friend bool operator!=(const struct ProcDetail& A, const struct ProcDetail& B);
    friend bool operator==(const struct ProcDetail& A, const struct ProcDetail& B);
    
} procDetail;


struct find_ProcDetail {
    unsigned int childPID;
    find_ProcDetail(unsigned int childPID) : childPID(childPID) {}
    bool operator () (const ProcDetail& p) const {
        return p.childPID == childPID;
    }
};


/********************************************************************
 * PipeExec
 * This adds standard pipe functionality to IPC_Base.  It supports:
 *  - CLI parsing & execution with all pipes (2>-style redirects excluded)
 *  - auto pipe connections (stdout of proc1 to stdin of proc2, etc)
 *  - interface to write to first proc and/or read from last proc in pipe chain
 *  - debugging output of stderr to /tmp files
 *
 * Examples:
 *
 * PipeExec p("cat /etc/pass | grep oo | cut -b1-20");
 * p.execute();
 * if (p.readAndMatch("root"))
 *     cout << "success" << endl;
 * p.closeAll();
 *
 * PipeExec p("sort | head");
 * p.execute();
 * char msg[] = "cat\ndog\nfish\n";
 * p.ipcWrite(msg, sizeof(msg));
 * p.closeWrite();
 * int bytesRead;
 * char buf[200];
 * while ((bytesRead = p.ipcRead(buf, sizeof(buf))))
 *     cout << buf;
 * p.closeAll();
 *******************************************************************/
class PipeExec : public IPC_Base {
    string origCommand;
    vector<procDetail> procs;
    bool bypassDestructor;
    bool dontCleanup;
    string errorDir;
    
    
public:
    /* structors */
    PipeExec(string command, unsigned int timeout = 120);
    ~PipeExec();
    
    /* execution */
    int execute(string procName = "", bool leaveFinalOutput = false, bool noDestruct = false, bool noErrToDisk = false, bool noTmpCleanup = false);
    bool execute2file(string toFile, string procName = "");
    
    /* administration */
    string errorOutput();
    void flushErrors();
    int closeRead();
    int closeWrite();
    int closeAll();
    void pickupTheKids();
};

#endif
