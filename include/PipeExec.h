
#ifndef PIPE_EXEC_H
#define PIPE_EXEC_H


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

    public:
        PipeExec(string cmd);
        procDetail execute();
        bool execute2file(string toFile);
        void dump();
};


#endif

