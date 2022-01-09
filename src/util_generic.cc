#include <iostream>
#include <fstream>
#include <unistd.h>
#include "time.h"
#include <syslog.h>
#include <openssl/md5.h>
#include <sys/stat.h>
#include "pcre++.h"
#include "math.h"

#include "pcre++.h"
#include "util_generic.h"

using namespace pcrepp;

string s(int number) {
    return(number == 1 ? "" : "s");
}


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


s_pathSplit pathSplit(string path) {
    s_pathSplit s;

    auto pos = path.rfind("/");
    s.file = path.substr(pos + 1);
    s.dir = path.substr(0, pos);

    if (!pos)
        s.dir = "/";

    if (pos == string::npos) {
        if (s.file == "..") {
            s.dir = "..";
            s.file = ".";
        }
        else 
            s.dir = ".";
    }

    return s;
}


string MD5file(string filename, bool quiet) {
    FILE *inputFile;

    if ((inputFile = fopen(filename.c_str(), "rb")) != NULL) {
        string message = "MD5 " + filename + "...";
        if (!quiet)
            cout << message << flush;

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

        if (!quiet) {
            string back = string(message.length(), '\b');
            string blank = string(message.length() , ' ');
            cout << back << blank << back << flush;
        }

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


string onevarsprintf(string format, string data) {
    char buffer[500];
    sprintf(buffer, format.c_str(), data.c_str());
    return buffer;
}


string approximate(double size) {
    int index = 0;
    char unit[] = {'B', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};

    while (size >= 1024 && index++ <= sizeof(unit)) {
        size /= 1024;
    }

    char buffer[150];
    sprintf(buffer, index > 2 ? "%.01f" : "%.0f", index > 2 ? size : floor(size));
    return(string(buffer) + unit[index]);
}



string seconds2hms(unsigned long seconds) {
    string result;
    int unit[] = {3600, 60, 1};
    bool dataAdded = false;

    for (int index = 0; index < sizeof(unit) / sizeof(unit[0]); ++index) {
        if (seconds >= unit[index]) {
            double value = floor(seconds / unit[index]);
            double leftover = seconds % unit[index];
            seconds = leftover;

            char buffer[50];
            sprintf(buffer, "%02.0f", value);
            result += string(dataAdded ? ":" : "") + buffer;
            dataAdded = true;
        }
        else {
            result += dataAdded ? ":00" : index == (sizeof(unit) / sizeof(unit[0])) - 1 ? "  " : "   ";
        }
    }

    return(result.length() ? result : "        ");
}


string timeDiff(unsigned long start, unsigned long end, int maxUnits) {
    unsigned long offset = end - start;
    int unitsUsed = 0;
    string result;
    map<unsigned long, string> units { 
        { 31556952, "year" },
        { 2592000, "month" },
        { 604800, "week" },
        { 86400, "day" },
        { 3600, "hour" },
        { 60, "minute" },
        { 1, "second" } };

    for (auto unit_it = units.rbegin(); unit_it != units.rend(); ++unit_it) {
        if (offset >= unit_it->first) {
            int value = floor(offset / unit_it->first);
            int leftover = offset % unit_it->first;

            result += (result.length() ? ", " : "") + to_string(value) + " " + unit_it->second + (value == 1 ? "" : "s");
            offset = leftover;

            if (++unitsUsed == maxUnits)
                break;
        }
    }

    return(result.length() ? result : "0 seconds");
}


string dw(int which) {
    const char* DOW[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    return(DOW[which]);
}


void mkdirp(string dir) {
    struct stat statBuf;

    if (stat(dir.c_str(), &statBuf) == -1) {
        char data[1500];
        strcpy(data, dir.c_str());
        char *p = strtok(data, "/");
        string path;

        while (p) {
            path += string("/") + p;   

            if (stat(path.c_str(), &statBuf) == -1)
               mkdir(path.c_str(), 0755);

            p = strtok(NULL, "/");
        }
    }
}


string trimSpace(const string &s) {
    auto start = s.begin();
    while (start != s.end() && isspace(*start))
        start++;

    auto end = s.end();
    do {
        end--;
    } while (distance(start, end) > 0 && isspace(*end));

    return string(start, end + 1);
}


string trimQuotes(string s) {
    Pcre reg("([\'\"]+)(.+?)\\g1");

    if (reg.search(s) && reg.matches())
        return reg.get_match(1);

    return s;
}


int varexec(string fullCommand) {
    Pcre cmdRE("('.*?'|\".*?\"|\\S+)", "g");
    char *params[300];

    int index = 0;
    size_t pos = 0;
    while (pos <= fullCommand.length() && cmdRE.search(fullCommand, pos)) {
        pos = cmdRE.get_match_end(0);
        ++pos;

        params[index] = (char *)malloc(cmdRE.get_match(0).length() + 1);
        strcpy(params[index++], trimQuotes(cmdRE.get_match(0)).c_str());
    }
    params[index] = NULL;

    if (!index) {
        cerr << "********* EXEC NO CMD ************" << endl;
        exit(1);
    }

    if (index == 1)
        execlp(params[0], "");

    execvp(params[0], params);

    cerr << "********* EXEC FAILED ************" << endl;
    exit(1);
}
