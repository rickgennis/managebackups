
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
 * PipeExec p("cat /etc/pass | grep oo | cut -b1-20");
 * p.execute();
 * if (p.readAndMatch("root"))
 *     cout << "success" << endl;
 * p.closeAll();
 *
 * PipeExec p("sort | head");
 * p.execute();
 * char msg[] = "cat\ndog\nfish\n";
 * p.writeProc(msg, sizeof(msg));
 * p.closeWrite();
 * int bytesRead;
 * while ((bytesRead = p.readProc(buf, sizeof(buf))))
 *     cout << buf;
 * p.closeAll();
 *
 */

using namespace std;


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

        void execute(string procName = "");    // procName is used to make a unique subdir under /tmp for STDERR output
        bool execute2file(string toFile, string procName);

        ssize_t readProc(void *buf, size_t count);
        ssize_t writeProc(const void *buf, size_t count);

        void readAndTrash();
        bool readAndMatch(string matchStr);
        
        int closeWrite();
        int closeRead();
        int closeAll();
        void dump();
};


#endif

