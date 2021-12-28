#include <iostream>
#include <fstream>
#include "time.h"
#include <syslog.h>
#include <openssl/md5.h>

#include "util.h"


void log(string message) {
    syslog(LOG_CRIT, "%s", message.c_str());

    time_t now;
    char timeStamp[100];

    now = time(NULL);
    strftime(timeStamp, sizeof(timeStamp), "%b %d %Y %H:%M:%S ", localtime(&now));

    ofstream logFile;
    logFile.open("managebackups.log", ios::app);

    if (logFile.is_open()) {
        logFile << string(timeStamp) << message << endl;
        logFile.close();
    }
}


string addSlash(string str) {
    return(str.length() && str[str.length() - 1] == '/' ? str : str + "/");
}


string MD5file(string filename) {
    FILE *inputFile;

    if ((inputFile = fopen(filename.c_str(), "rb")) != NULL) {
        unsigned char data[65536];
        int bytesRead;
        MD5_CTX md5Context;

        MD5_Init(&md5Context);
        while ((bytesRead = fread(data, 1, 65536, inputFile)) != 0)
            MD5_Update(&md5Context, data, bytesRead);

        fclose(inputFile);
        unsigned char md5Result[MD5_DIGEST_LENGTH];
        MD5_Final(md5Result, &md5Context);

        char tempStr[MD5_DIGEST_LENGTH * 2];
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
            sprintf(tempStr+(2*i), "%02x", md5Result[i]);
        return(tempStr);
    }

    return "";
}


string MD5string(string origString) {
        unsigned char data[65536];
        unsigned char md5Result[MD5_DIGEST_LENGTH];
        char* c_origString = const_cast<char*>(origString.c_str());

        MD5_CTX md5Context;
        MD5_Init(&md5Context);
        MD5_Update(&md5Context, c_origString, origString.length());
        MD5_Final(md5Result, &md5Context);

        char tempStr[MD5_DIGEST_LENGTH * 2];
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
            sprintf(tempStr+(2*i), "%02x", md5Result[i]);
        return(tempStr);
}

