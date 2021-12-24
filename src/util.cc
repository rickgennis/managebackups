#include <iostream>
#include <fstream>
#include "time.h"
#include <syslog.h>

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
