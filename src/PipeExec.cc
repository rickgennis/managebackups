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

    while (p) {
        procs.insert(procs.end(), procDetail(trimSpace(p)));
        p = strtok(NULL, "|");
    }

    free(data);
}


string ProcDetail::fdDetail() {
    return (to_string(fd[READ_END]) + ", " + to_string(fd[WRITE_END]));
}


void PipeExec::dump() {
    for (auto c = procs.begin() ; c != procs.end(); ++c) 
        cerr << "DUMP: [" << trimSpace(c->command) << "] " << c->fdDetail() << endl;;
}


procDetail PipeExec::execute() {
    int devNull = open("/dev/null", O_WRONLY);

    procs.insert(procs.begin(), procDetail("head"));
    for (auto proc_it = procs.begin(); proc_it != procs.end(); ++proc_it) {

        // last loop
        if (*proc_it == procs[procs.size() - 1]) {
            DUP2(procs[0].fd[WRITE_END], WRITE_END);
            varexec(proc_it->command);
        }
        else  {
            // create the pipe & fork
            pipe(proc_it->fd);
            if ((proc_it->childPID = fork())) {
                if (devNull > 0)
                    DUP2(devNull, 2);

                // PARENT - all subsequent parents
                if (*proc_it != procs[0]) {
                    close(proc_it->fd[READ_END]);
                    DUP2(proc_it->fd[WRITE_END], WRITE_END);
                    varexec(proc_it->command);
                }

                // PARENT - first parent
                close(proc_it->fd[WRITE_END]);
                return *proc_it;
            }
        }
        
        // CHILD 
        if (devNull > 0)
            DUP2(devNull, 2);

        if (*proc_it != procs[0]) {
            close(proc_it->fd[WRITE_END]);
            DUP2(proc_it->fd[READ_END], READ_END);
        }
    }

    exit(1);  // never get here
}


bool PipeExec::execute2file(string toFile) {
    int outFile;
    int bytesRead;
    int bytesWritten;
    int pos;
    bool success = false;
    char data[16 * 1024];

    if ((outFile = open(toFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR)) > 0) {
        auto proc = execute();

        while ((bytesRead = read(proc.fd[0], data, sizeof(data)))) {
            pos = 0;
            while (((bytesWritten = write(outFile, data+pos, bytesRead)) < bytesRead) && (errno == EINTR)) {
                pos += bytesRead;
            }
        }

        close(outFile);
        success = true;
    }
    else
        log("Unable to write to " + toFile + ": " + strerror(errno));

    while (wait(NULL) > 0);

    return success;
}


