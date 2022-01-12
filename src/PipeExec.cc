#include <string>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <vector>
#include "pcre++.h"
#include "util_generic.h"
#include "PipeExec.h"

#define READ_END STDIN_FILENO
#define WRITE_END STDOUT_FILENO

#define DUP2(x,y) while (dup2(x,y) < 0 && errno == EINTR);

using namespace std;
using namespace pcrepp;



bool operator!=(const struct ProcDetail& A, const struct ProcDetail& B) {
    return !(A.command == B.command && A.childPID == B.childPID);
}

bool operator==(const struct ProcDetail& A, const struct ProcDetail& B) {
    return !(A != B);
}
 


PipeExec::PipeExec(string fullCommand) {
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
    if (procs.size() > 0) {
        close(procs[0].fd[READ_END]);
        close(procs[0].fd[WRITE_END]);
    }

    while (numProcs--)
        waitpid(-1, NULL, WNOHANG);
}


string ProcDetail::fdDetail() {
    return (to_string(fd[READ_END]) + (fcntl(fd[READ_END], F_GETFD) != -1 ? "o" : "c") + ", " + 
            to_string(fd[WRITE_END]) + (fcntl(fd[WRITE_END], F_GETFD) != -1 ? "o" : "c"));
}


void PipeExec::dump() {
    for (auto c = procs.begin() ; c != procs.end(); ++c) 
        cerr << "DUMP: [" << trimSpace(c->command) << "] " << c->fdDetail() << endl;;

}


int PipeExec::executeWrite(string title) {
    procs.insert(procs.begin(), procDetail("head"));

    string errorFilename = TMP_OUTPUT_DIR + title + "/";
    mkdirp(errorFilename.c_str());

    int commandIdx = -1;
    for (auto proc_it = procs.begin(); proc_it != procs.end(); ++proc_it) {
        ++commandIdx;
        string commandPrefix = proc_it->command.substr(0, proc_it->command.find(" "));

        // last loop
        if (*proc_it == procs[procs.size() - 1]) {
            // redirect stderr to a file
            int errorFd = open(string(errorFilename + to_string(commandIdx) + "." + safeFilename(commandPrefix) + ".stderr").c_str(), 
                O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
            if (errorFd > 0)
                DUP2(errorFd, 2);

            // redirect stdout to a file
            int outFd = open(string(errorFilename + to_string(commandIdx) + "." + safeFilename(commandPrefix) + ".stdout").c_str(), 
                O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
            if (outFd > 0)
                DUP2(outFd, 1);

            // dup and close remaining fds
            auto back_it = proc_it - 1;
            DUP2(back_it->fd[READ_END], READ_END);
            close(procs[0].fd[WRITE_END]);

            // exec the last process
            varexec(proc_it->command);
        }
        else  {
            // create the pipe & fork
            pipe(proc_it->fd);
            if ((proc_it->childPID = fork())) {

                // PARENT - all subsequent parents
                if (*proc_it != procs[0]) {
                    // redirect stderr to a file
                    int errorFd = open(string(errorFilename + to_string(commandIdx) + "." + safeFilename(commandPrefix) + ".stderr").c_str(), 
                            O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                    if (errorFd > 0)
                        DUP2(errorFd, 2);

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
                return proc_it->fd[WRITE_END];
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


int PipeExec::executeRead(string title) {
    procs.insert(procs.begin(), procDetail("head"));

    string errorFilename = TMP_OUTPUT_DIR + title + "/";
    mkdirp(errorFilename.c_str());

    int commandIdx = -1;
    for (auto proc_it = procs.begin(); proc_it != procs.end(); ++proc_it) {
        ++commandIdx;
        string commandPrefix = proc_it->command.substr(0, proc_it->command.find(" "));

        // last loop
        if (*proc_it == procs[procs.size() - 1]) {
            int errorFd = open(string(errorFilename + to_string(commandIdx) + "." + safeFilename(commandPrefix) + ".stderr").c_str(), 
                O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
            if (errorFd > 0)
                DUP2(errorFd, 2);
            DUP2(procs[0].fd[WRITE_END], WRITE_END);
            varexec(proc_it->command);
        }
        else  {
            // create the pipe & fork
            pipe(proc_it->fd);
            if ((proc_it->childPID = fork())) {

                // PARENT - all subsequent parents
                if (*proc_it != procs[0]) {
                    int errorFd = open(string(errorFilename + to_string(commandIdx) + "." + safeFilename(commandPrefix) + ".stderr").c_str(), 
                            O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                    if (errorFd > 0)
                        DUP2(errorFd, 2);
                    close(proc_it->fd[READ_END]);
                    DUP2(proc_it->fd[WRITE_END], WRITE_END);
                    varexec(proc_it->command);
                }

                // PARENT - first parent
                close(proc_it->fd[WRITE_END]);
                return proc_it->fd[READ_END];
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


bool PipeExec::execute2file(string toFile, string title) {
    int outFile;
    int bytesRead;
    int bytesWritten;
    int pos;
    bool success = false;
    char data[16 * 1024];

    if ((outFile = open(toFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR)) > 0) {
        auto fd = executeRead(title);

        while ((bytesRead = read(fd, data, sizeof(data)))) {
            pos = 0;
            while (((bytesWritten = write(outFile, data+pos, bytesRead)) < bytesRead) && (errno == EINTR)) {
                pos += bytesRead;
            }
        }

        close(outFile);
        close(fd);
        success = true;
    }
    else
        log("Unable to write to " + toFile + ": " + strerror(errno));

    while (wait(NULL) > 0);

    return success;
}


