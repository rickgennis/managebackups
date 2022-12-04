#include <iostream>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

#include "network.h"
#include "util_generic.h"


using namespace std;

void tcpSocket::flush() {
    if (socketFd < 3)
        fsync(socketFd);

    if (readFd < 3)
        fsync(readFd);
}


tcpSocket::tcpSocket(int port, int backlog) {
    readFd = -1;

    if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) > 0) {
        int option = 1;

        if (!setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option))) {
            int addrlen = sizeof(address);
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(port);

            if (::bind(socketFd, (struct sockaddr*)&address, sizeof(address)) == 0) {
                if (!listen(socketFd, backlog))  {
                    cout << "[server] listening on *:" << port << endl;
                    return;
                }
                else {
                    socketFd = 0;
                    throw(string("listen(server): ") + string(strerror(errno)));
                }
            }
            else {
                socketFd = 0;
                throw(string("bind(server): ") + string(strerror(errno)));
            }
        }
        else {
            socketFd = 0;
            throw(string("setsockopt(server): ") + string(strerror(errno)));
        }
    }
    else {
        socketFd = 0;
        throw(string("socket(server): ") + string(strerror(errno)));
    }
}


tcpSocket::tcpSocket(string server, int port) {
    readFd = -1;

    if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) > 0) {
        address.sin_family = AF_INET;
        address.sin_port = htons(port);

        if (inet_pton(AF_INET, server.c_str(), &address.sin_addr) > 0) {
            if (!connect(socketFd, (struct sockaddr*)&address, sizeof(address)))
                return;
            else {
                socketFd = 0;
                throw(string("connect(client): ") + string(strerror(errno)));
            }
        }
        else {
            socketFd = 0;
            throw(string("inet_pton(client): ") + string(strerror(errno)));
        }
    }
    else {
        socketFd = 0;
        throw(string("socket(client): ") + string(strerror(errno)));
    }
}


tcpSocket::tcpSocket(int fd) {
    readFd = -1;
    socketFd = fd;
    address.sin_family = AF_INET;
}


void tcpSocket::setReadFd(int fd) {
    readFd = fd;
}


tcpSocket tcpSocket::accept(int timeoutSecs) {
    int addrlen = sizeof(address);

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socketFd, &readSet);

    struct timeval tv;
    tv.tv_sec = timeoutSecs;
    tv.tv_usec = 0;
    
    int result;
    if ((result = select(socketFd + 1, &readSet, NULL, NULL, &tv)) < 1)
        exit(0);

    int newFd;
    if ((newFd = ::accept(socketFd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) > 0) {  
        tcpSocket client(newFd);
        return client; 
    }

    perror("accept()");
    exit(1);
}


tcpSocket::~tcpSocket() {
    closeConnection();
}


void tcpSocket::closeConnection() {
    close(socketFd);
}


size_t tcpSocket::write(const void *data, size_t count) {
    ssize_t bytesWritten;
    ssize_t totalBytesWritten = 0;

    while (count && (bytesWritten = 
        (socketFd > 2 ? ::write(socketFd, (char*)data + totalBytesWritten, count - totalBytesWritten) :
        fwrite(data, 1, count, stdout))) > 0) {

        count -= bytesWritten;
        totalBytesWritten += bytesWritten;
    }

    return totalBytesWritten;
}


size_t tcpSocket::write(const char *data) {
    return (write(data, strlen(data)));
}


size_t tcpSocket::write(long data) {
    long netLong = htonl(data);
    log("tcpSocket::write(8) writing " + to_string(data));
    return (write(&netLong, 8));
}


size_t tcpSocket::read(void *data, size_t count) {
    auto bufLen = strBuf.length();
    size_t dataLen = 0;

    if (bufLen) {
        dataLen = bufLen > count ? count : bufLen;
        memcpy(data, strBuf.c_str(), dataLen);
        strBuf.erase(0, dataLen);
        
        if (count <= dataLen)
            return(dataLen);

        count -= dataLen;
    }

    return (::read(readFd > -1 ? readFd : socketFd, (char*)data + dataLen, count));
}


long tcpSocket::read() {
    long data;
    int pos = 0;

    while (pos < 8)
        pos += read((char*)&data + pos, 8 - pos);

    long temp = ntohl(data);
    return(temp);
}


string tcpSocket::readTo(string delimiter) {
    int attempt = 5;

    while (1) {
        if (strBuf.length()) {
            size_t index;

            if ((index = strBuf.find(delimiter)) != string::npos) {
                log("readTo() strBuf length is " + to_string(strBuf.length()));
                string result = strBuf.substr(0, index);
                strBuf.erase(0, index + delimiter.length());
                return result;
            }
            else
                log("readTo() no delimit found (" + to_string(strBuf.length()) + "); reading [" + strBuf + "]");
        }

        size_t bytes = read(rawBuf, sizeof(rawBuf));
        string tempStr(rawBuf, bytes);
        log("readTo() read [" + tempStr + "]");
        strBuf += tempStr;

        if (!bytes && !--attempt) {
            sleep(1);
            cerr << "failed socket read" << endl;
            log("failed socket read");
            //exit(10);
        }
    }
}


void tcpSocket::readToFile(string filename) {
    long uid = read();
    long gid = read();
    long mode = read();
    long mtime = read();

    //cout << "server: receive " << filename << ", mode:" << mode << ", uid: " << uid << ", gid: " << gid << ", mtime: " << mtime << endl;

    // handle directories that are specifically sent
    if (S_ISDIR(mode)) {
        mkdirp(filename);
        chmod(filename.c_str(), mode);
        chown(filename.c_str(), uid, gid);

        struct utimbuf timeBuf;
        timeBuf.actime = timeBuf.modtime = mtime;
        utime(filename.c_str(), &timeBuf);

        //cout << "server: created directory " << filename << endl;
        return;
    }

    // handle directories that are inherent in the filename
    string dirName = filename.substr(0, filename.find_last_of("/"));
    //cout << "making " << dirName << ": " << mkdirp(dirName);  // need a mode, gid and uid here
                                                              
    if (S_ISLNK(mode)) {
        char target[1025];
        long bytes = read();
        read(target, bytes);
        target[bytes] = 0;
        if (symlink(target, filename.c_str())) {
            cerr << "unable to create symlink (" << filename << "): ";
            perror("");
            return;
        }
        chmod(filename.c_str(), mode);
        chown(filename.c_str(), uid, gid);

        //cout << "server: created symlink " << filename << " (-> " << target << ")" << endl;
        return;
    }

    // handle files
    auto bytesRemaining = read();
    //cout << "server: receiving " << filename << " as " << bytesRemaining << " bytes" << endl;

    FILE *dataf;
    auto bufSize = sizeof(rawBuf);
    dataf = fopen(filename.c_str(), "wb");

    /* 
     * To maintain the network protocol with the client we have to read 'bytesRemaining' bytes
     * even if our fopen() failed and we can't save them to the local disk. That way all the 
     * other read()s in this network connection still line up and subsequent files may transfer
     * even if there was an issue with this one.
     */

    //cout << "\tserver: created " << filename << endl;
    while (bytesRemaining) {
        size_t readSize = bytesRemaining < bufSize ? bytesRemaining : bufSize;
        size_t bytesRead = read(rawBuf, readSize);
        bytesRemaining -= bytesRead;

        if (dataf != NULL) {
            if (fwrite(rawBuf, 1, bytesRead, dataf) < bytesRead) {
                perror(filename.c_str());
                fclose(dataf);
                break;
            }
        }
    }

    if (dataf != NULL) {
        fclose(dataf);
        chown(filename.c_str(), uid, gid);
        chmod(filename.c_str(), mode);

        struct utimbuf timeBuf;
        timeBuf.actime = timeBuf.modtime = mtime;
        utime(filename.c_str(), &timeBuf);
    }

    //cout << "server: completed " << filename << endl;
}


void tcpSocket::sendRawFile(fileInfo& fi) {
    FILE *dataf;

    //cout << "client: sending " << fi.filename << " as " << fi.size << " bytes." << endl;

    if ((dataf = fopen(fi.filename.c_str(), "rb")) != NULL) {
        write(fi.size);
        auto readRemaining = fi.size;
        auto bufSize = sizeof(rawBuf);
        while (readRemaining) {
            size_t readSize = readRemaining < bufSize ? readRemaining : bufSize;
            auto bytesRead = fread(rawBuf, 1, readSize, dataf);
            readRemaining -= bytesRead;

            //cout << "client send: " << fi.filename << " - " << "readSize:" << readSize << ", bytesread:" << bytesRead << ", readRemaining:" << readRemaining << endl;
            write(rawBuf, bytesRead);
        }

        fclose(dataf);
        //cout << "client: completed " << fi.filename << " with " << readRemaining << " bytes left." << endl;
    }
    else {
        write((long)0);
        cerr << "error: unable to read " << fi.filename << endl;
    }
}


void fileTransport::sendFullContents() {
    if (fi.mode > 0) {
        //cout << "client: sending " << fi.filename << " mode as " << fi.mode << endl;

        server.write(fi.uid);
        server.write(fi.gid);
        server.write(fi.mode);
        server.write(fi.mtime);

        if (isSymLink()) {
            char target[1024];
            long bytes = readlink(fi.filename.c_str(), target, sizeof(target));
            server.write(bytes);
            server.write(target, bytes);
        }
        else
            if (!isDir()) {
                server.sendRawFile(fi);
            }
    }
    else
        server.write((long)0);
}


fileTransport::fileTransport(tcpSocket& serverSock) : server(serverSock) { }


int fileTransport::statFile(string file) {
    struct stat statData;
    int result;
    fi.mode = 0;

    fi.filename = file;
    if (!(result = lstat(file.c_str(), &statData))) {
        fi.mode = statData.st_mode;
        fi.uid = statData.st_uid;
        fi.gid = statData.st_gid;
        fi.size = statData.st_size;
        fi.mtime = statData.st_mtime;
    }

    return result;
}


void fileTransport::sendStatInfo() {
    server.write(string(fi.filename + NET_DELIM).c_str());
    server.write(fi.mtime);
}


bool fileTransport::isDir() {
    return(S_ISDIR(fi.mode));
}


bool fileTransport::isSymLink() {
    return(S_ISLNK(fi.mode));
}


