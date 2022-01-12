
#ifndef PIPE_EXEC_H
#define PIPE_EXEC_H

#include <string>
#include <vector>

/****************************************************************
 * PipeExec
 *
 * Execute an arbitary length chain of piped processes with the
 * ability to write data (stdin) to the first one and read
 * data back out (stdout) from the last.
 *
 * Examples:
 *
 * PipeExec proc("cat /etc/pass | grep oo | cut -b1-20");
 * proc.execute();
 * if (proc.readAndMatch("root"))
 *     cout << "success" << endl;
 * proc.closeall();
 *
 * PipeExec proc("sort | head");
 * auto fds = proc.execute();
 * write(fds.write, "cat\ndog\nfish", sizeof("cat\ndog\nfish\n"));
 * close(fds.write);
 * int bytesRead;
 * while ((bytesRead = read(fds.read, buf, sizeof(buf))))
 *     cout << buf;
 * proc.closeall();
 *
 */



using namespace std;

struct fdPair {
    int read;
    int write;

    fdPair(int r, int w) { read = r;  write = w; }
};


typedef struct ProcDetail {
    int fd[2];
    int readfd[2];
    string command;
    unsigned int childPID;

    ProcDetail(string cmd) { command = cmd; fd[0] = fd[1] = readfd[0] = readfd[1] = childPID = 0; }
    string fdDetail();

    friend bool operator!=(const struct ProcDetail& A, const struct ProcDetail& B);
    friend bool operator==(const struct ProcDetail& A, const struct ProcDetail& B);

} procDetail;


class PipeExec {
    vector<procDetail> procs;
    int numProcs;

    public:
        PipeExec(string cmd);
        ~PipeExec();

        fdPair execute(string procName = "");
        bool execute2file(string toFile, string procName);
        void readAndTrash();
        bool readAndMatch(string matchStr);
        int closeall();
        void dump();
};


#endif

