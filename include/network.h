
#ifndef NETWORK_H
#define NETWORK_H

#include <arpa/inet.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using std::string;

#define BUFFER_SIZE     (1024 * 32)
#define NET_DELIM   ";\n"
#define NET_OVER    "///;/"
#define NET_OVER_DELIM    (string(NET_OVER) + NET_DELIM).c_str()

struct fileInfo {
    string filename;
    long mtime;
    long mode;
    long uid;
    long gid;
    long size;
};


class tcpSocket {
    int socketFd;
    struct sockaddr_in address;
    string strBuf;
    char rawBuf[BUFFER_SIZE];
    char responseBuf[BUFFER_SIZE];
    
    public:
        tcpSocket(int port, int backlog = 50);
        tcpSocket(string server, int port);
        tcpSocket(int fd, string bogus);
        ~tcpSocket();

        tcpSocket accept(int timeoutSecs);

        size_t write(const void *data, size_t count);
        size_t write(const char *data);
        size_t write(long data);
        void sendRawFile(fileInfo& fi);

        size_t read(void *data, size_t count);
        long read();
        string readTo(string delimiter);
        void readToFile(string filename);

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

