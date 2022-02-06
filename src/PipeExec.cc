#include <string>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <vector>
#include "pcre++.h"
#include "util_generic.h"
#include "colors.h"
#include "globals.h"
#include "PipeExec.h"

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
 


PipeExec::PipeExec(string fullCommand) {
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


int PipeExec::execute(string procName, bool leaveOutput, bool noDestruct) {
    bypassDestructor = noDestruct;
    procs.insert(procs.begin(), procDetail("head"));

    string errorFilename = string(TMP_OUTPUT_DIR) + "/" + safeFilename(procName) + "/";
    mkdirp(errorFilename);

    DEBUG(3, "preparing full command [" << origCommand << "]");

    int commandIdx = -1;
    for (auto proc_it = procs.begin(); proc_it != procs.end(); ++proc_it) {
        ++commandIdx;
        string commandPrefix = proc_it->command.substr(0, proc_it->command.find(" "));
        string stderrFname = errorFilename + to_string(commandIdx) + "." + safeFilename(commandPrefix) + ".stderr";

        // last loop
        if (*proc_it == procs[procs.size() - 1]) {
            DEBUG(3, "executing final command [" << proc_it->command << "]");

            // redirect stderr to a file
            if (procName.length()) {
                int errorFd = open(stderrFname.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                if (errorFd > 0) 
                    DUP2(errorFd, 2);
                else { 
                    string msg = "warning: unable to redirect STDERR of subprocess because " + stderrFname + " isn't writable";
                    SCREENERR("\n" << msg);
                    log(msg);
                }
            }

            // dup and close remaining fds
            //auto back_it = proc_it - 1;
            auto back_it = proc_it - 1;
            DUP2(back_it->fd[READ_END], READ_END);
            close(procs[0].fd[WRITE_END]);
            close(procs[0].readfd[READ_END]);

            if (!leaveOutput)
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
                    DEBUG(3, "executing mid command [" << proc_it->command << "] with pipes " << proc_it->fd[0] << " & " << proc_it->fd[1]);

                    // redirect stderr to a file
                    if (procName.length()) {
                        int errorFd = open(stderrFname.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                        if (errorFd > 0) 
                            DUP2(errorFd, 2);
                        else {
                            string msg = "warning: unable to redirect STDERR of subprocess because " + stderrFname + " isn't writable";
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

    DEBUG(3, "toFile=" << toFile << "; procName=" << procName);

    if ((outFile = open(toFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR)) > 0) {
        execute(procName);

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
    return write(procs[0].fd[WRITE_END], buf, count);
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

