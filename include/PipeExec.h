
#ifndef PIPE_EXEC_H
#define PIPE_EXEC_H

#include <string>
#include <vector>

using namespace std;


typedef struct ProcDetail {
    int fd[2];
    string command;
    unsigned int childPID;

    ProcDetail(string cmd) { command = cmd; fd[0] = fd[1] = childPID = 0; }
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

        int executeWrite(string title);
        int executeRead(string title);    // title is for error logging as a subdirectory name
        bool execute2file(string toFile, string title);
        void dump();
};


#endif

