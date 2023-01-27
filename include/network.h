
#ifndef NETWORK_H
#define NETWORK_H

#include <arpa/inet.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using std::string;

#define BUFFER_SIZE     (1024 * 64)
#define NET_DELIM   ";\n"
#define NET_OVER    "///;/"
#define NET_OVER_DELIM    string(string(NET_OVER) + string(NET_DELIM)).c_str()

struct fileInfo {
    string filename;
    long mtime;
    long mode;
    long uid;
    long gid;
    __int64_t size;
};


class tcpSocket {
    int socketFd;
    int readFd;  // not used for a socket; only for when this class is used to talk to pipes
    unsigned int timeoutSecs;

    struct sockaddr_in address;
    string strBuf;
    char rawBuf[BUFFER_SIZE];
    
    public:
        tcpSocket(int port, int backlog, unsigned int timeout);
        tcpSocket(string server, int port, unsigned int timeout);
        tcpSocket(int fd, unsigned int timeout);
        ~tcpSocket();

        tcpSocket accept(unsigned int timeout = 120);

        void setReadFd(int fd);
        size_t write(const void *data, size_t count);
        size_t write(const char *data);
        size_t write(__int64_t data);
        void sendRawFile(fileInfo& fi);

        ssize_t read(void *data, size_t count);
        __int64_t read();
        string readTo(string delimiter);
        void readToFile(string filename);

        void flush();
        void closeConnection();
};


class fileTransport {
    tcpSocket& server;
    fileInfo fi;

    public:
        fileTransport(tcpSocket& serverSock);
        bool isDir();
        bool isSymLink();
        int statFile(string file);
        void sendStatInfo();
        void sendFullContents();
};


#endif

