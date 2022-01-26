#include <iostream>
#include <sstream>
#include <fstream>
#include <dirent.h>
#include <unistd.h>
#include "time.h"
#include <syslog.h>
#include <openssl/md5.h>
#include <algorithm>
#include <sys/stat.h>
#include "pcre++.h"
#include "math.h"
#include <sys/types.h>
#include <pwd.h>

#include "pcre++.h"
#include "util_generic.h"
#include "globals.h"


using namespace pcrepp;

string s(int number) {
    return(number == 1 ? "" : "s");
}


string cppgetenv(string variable) {
    char* c;

    c = getenv(variable.c_str());
    if (c == NULL)
        return "";
    else
        return c;
}


void log(string message) {
#ifdef ON_MAC
    if (!GLOBALS.logDir.length()) {  
        string defaultDir = "/var/log";

        ofstream logFile;
        logFile.open(defaultDir + "/managebackups.log", ios::app);
        if (logFile.is_open()) {
            logFile.close();
            GLOBALS.logDir = defaultDir;
        }
        else {
            string testFile = defaultDir + ".testfile." + to_string(GLOBALS.pid);
            logFile.open(testFile);
            if (logFile.is_open()) {
                logFile.close();
                unlink(testFile.c_str());
                GLOBALS.logDir = defaultDir;
            }
            else {
                struct passwd *pw = getpwuid(getuid());
                GLOBALS.logDir = string(pw->pw_dir);
            }
        }
    }

    time_t now;
    char timeStamp[100];

    now = time(NULL);
    strftime(timeStamp, sizeof(timeStamp), "%b %d %Y %H:%M:%S ", localtime(&now));

    ofstream logFile;
    logFile.open(GLOBALS.logDir + "/managebackups.log", ios::app);

    if (logFile.is_open()) {
        logFile << string(timeStamp) << message << endl;
        logFile.close();
    }
#else
    syslog(LOG_CRIT, "%s", message.c_str());
#endif
}


struct timeval mktimeval(unsigned long secs) { struct timeval t; t.tv_sec = secs; t.tv_usec = 0; return t; }


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

        char tempStr[MD5_DIGEST_LENGTH * 2 + 1];
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
            sprintf(tempStr+(2*i), "%02x", md5Result[i]);
        tempStr[MD5_DIGEST_LENGTH * 2] = 0;

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


string timeDiff(struct timeval start, struct timeval end, int maxUnits) {
    unsigned long totalus = (end.tv_sec * MILLION + end.tv_usec) - (start.tv_sec * MILLION + start.tv_usec);
    unsigned long secs = floor(1.0 * totalus / MILLION);
    unsigned long us = secs ? totalus % (secs * MILLION) : totalus;
    unsigned long offset = secs;
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

    // if the duration is less than a minute and microseconds
    // are also provided, include them
    if (secs < 60 && us) {
        char ms[10];
        sprintf(ms, "%.2f", 1.0 * us / MILLION);

        string sms = ms;
        if (sms.back() == '0')
            sms.pop_back();

        auto pos = result.find(" ");
        if (pos != string::npos) {
            sms.erase(0, 1);
            result.replace(pos, 0, sms);

            if (result.back() != 's')
                result += "s";
        }
        else
            result = sms + " seconds";
    }

    return(result.length() ? result : "0 seconds");
}


string dw(int which) {
    const char* DOW[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    return(DOW[which]);
}


int mkdirp(string dir) {
    struct stat statBuf;
    int result = 0;

    if (stat(dir.c_str(), &statBuf) == -1) {
        char data[1500];
        strcpy(data, dir.c_str());
        char *p = strtok(data, "/");
        string path;

        while (p) {
            path += string("/") + p;   

            if (stat(path.c_str(), &statBuf) == -1)
               result = mkdir(path.c_str(), 0775);

            if (result)
                return(result);

            p = strtok(NULL, "/");
        }
    }

    return 0;
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
    // split the command into a parameter list on spaces, except when quoted or escaped
    Pcre cmdRE("((?:([\'\"])(?:(?!\\g2).|(?:(?<=\\\\)[\'\"]))+(?<!\\\\)\\g2)|(?:\\S|(?:(?<=\\\\)\\s))+)", "g");
    char *params[400];
    string match;

    int index = 0;
    size_t pos = 0;
    while (pos <= fullCommand.length() && cmdRE.search(fullCommand, pos)) {
        pos = cmdRE.get_match_end(0);
        ++pos;
        match = cmdRE.get_match(0);

        // if its a valid string
        if (match.length() &&
                // not quoted
                !((match.front() == '\'' || match.front() == '\"') && match.front() == match.back()) &&
                // and has shell wildcards
                (match.find("*") != string::npos || match.find("?") != string::npos)) {
                
            // then replace the parameter will a wildcard expansion of the maching files
            auto files = expandWildcardFilespec(trimQuotes(match));

            for (auto entry: files) {
                // no need to free() this because we're going to exec()
                params[index] = (char *)malloc(entry.length() + 1);

                // add each matching file as a parameter for exec()
                strcpy(params[index++], entry.c_str());
            }
        } 
        else {
            size_t altpos;
            // escaping is handled above by cmdRE and is complete by the time we get here
            // so now we remove any remaining backslashes in the string
            if ((altpos = match.find("\\")) != string::npos)
                match.erase(altpos, 1);

            params[index] = (char *)malloc(match.length() + 1);

            // and drop any quotes that are on the ends
            strcpy(params[index++], trimQuotes(match).c_str());
        }
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


string safeFilename(string filename) {
    Pcre search1("[\\s#;\\/\\\\]+", "g");   // these characters get converted to underscores
    Pcre search2("[\\?\\!\\*]+", "g");      // these characters get removed

    string tempStr = search1.replace(filename, "_");
    return search2.replace(tempStr, "");
}


vector<string> expandWildcardSub(string fileSpec, string baseDir, int index) {
    vector<string> result;
    vector<string> parts;
    stringstream tokenizer(fileSpec);
    string tempStr;
    string resultBaseDir = baseDir;

    // break the fileSpec into sections delimited by slashes
    while (getline(tokenizer, tempStr, '/'))
        parts.push_back(tempStr);

    // if no wildcards are present in the entire string we need to return the original filespec
    if (index == parts.size()) {
        result.insert(result.end(), fileSpec);
        return(result);
    }

    // get the current fileSpec section we're working on
    string currentSpec = parts[index];

    // if there are no wildcards (asterisk or question marks) in the current section
    // of the fileSpec we can recurse directly to the next section.
    auto asterisk = currentSpec.find("*");
    auto questionmark = currentSpec.find("?");
    if (asterisk == string::npos && questionmark == string::npos) {
        return expandWildcardSub(fileSpec, baseDir + (baseDir.back() == '/' ? "" : "/") + currentSpec, index+1);
    }
    else {
        // otherwise we need to read the directory entry by entry and see which match
        // the currentSpec
        DIR *c_dir;
        struct dirent *c_dirEntry;

        if ((c_dir = opendir(baseDir.c_str())) != NULL) {

            // change the currentSpec into a PCRE
            strReplaceAll(currentSpec, ".", "\\.");
            strReplaceAll(currentSpec, "?", ".");
            strReplaceAll(currentSpec, "*", ".*");
            currentSpec = "^" + currentSpec + "$";

            Pcre matchSpec(currentSpec);
            struct stat statData;

            // loop through files in this directory
            while ((c_dirEntry = readdir(c_dir)) != NULL) {
                if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                    continue;

                // does the current file match the current fileSpec's regex?
                if (matchSpec.search(c_dirEntry->d_name)) {
                    if (!stat(string(baseDir + "/" + c_dirEntry->d_name).c_str(), &statData)) {

                        // if this matching dir entry is a subdirectory, call ourselves recursively
                        // and add those results to our our result list
                        if ((statData.st_mode & S_IFMT) == S_IFDIR) {
                            auto subResult = expandWildcardSub(fileSpec, baseDir + (baseDir.back() == '/' ? "" : "/") + 
                                    c_dirEntry->d_name, index+1);
                            result.insert(result.end(), subResult.begin(), subResult.end());
                        }
                        else {
                            // if it's not a subdirectory just add this entry to the result list
                            result.insert(result.end(), resultBaseDir + (resultBaseDir.back() == '/' ? "" : "/") + c_dirEntry->d_name);
                        }
                    }
                    else {
                        log("expandWildcard: unable to stat " + baseDir + "/" + c_dirEntry->d_name);
                    }
                }
            }
        
            closedir(c_dir);
        } 
    }

    return(result);
}


vector<string> expandWildcardFilespec(string fileSpec) {
    string baseDir;

    if (fileSpec.front() == '/')
        baseDir = "/";
    else
        baseDir = ".";

    auto fileList = expandWildcardSub(fileSpec, baseDir, 0);
    for (auto file_it = fileList.begin(); file_it != fileList.end(); ++file_it)
        if (file_it->length() > 2 && file_it->substr(0, 2) == "./")
            file_it->erase(0, 2);

    return fileList;
}


void strReplaceAll(string& s, string const& toReplace, string const& replaceWith) {
    ostringstream oss;
    size_t pos = 0;
    size_t prevPos = pos;

    while (1) {
        prevPos = pos;
        pos = s.find(toReplace, pos);
        if (pos == string::npos)
            break;
        oss << s.substr(prevPos, pos - prevPos);
        oss << replaceWith;
        pos += toReplace.size();
    }

    oss << s.substr(prevPos);
    s = oss.str();
}


string locateBinary(string app) {
    string tempStr;
    string path = cppgetenv("PATH");
    stringstream tokenizer(path);
    vector<string> parts;

    if (app.find("/") == string::npos) {
        while (getline(tokenizer, tempStr, ':'))
            parts.push_back(tempStr);

        for (auto it: parts) {
            string binary = string(it) + "/" + app;
            if (!access(binary.c_str(), X_OK))
                return binary;
        }
    }
    else
        if (!access(app.c_str(), X_OK))
            return(app);

    log("unable to locate/execute '" + app + "' command");
    return "";
}


bool str2bool(string text) {
    Pcre regTrue("^\\s*(t|true|y|yes|1)\\s*$", "i");

    return(regTrue.search(text));
}


string vars2MY(int month, int year) {
    char monthName[12][15] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

    return(string(monthName[month - 1]) + " " + to_string(year));
}


bool mtimesAreSameDay(time_t m1, time_t m2) {
    struct tm *temp = localtime(&m1);  // localtime always returns the same internal pointer
    struct tm tM1 = *temp;             // so two calls to it require a temp to hold the first's data
    struct tm *tM2 = localtime(&m2);

    return (tM1.tm_year == tM2->tm_year && 
            tM1.tm_mon  == tM2->tm_mon && 
            tM1.tm_mday == tM2->tm_mday);
}


string horizontalLine(int length) {
    char dash[5];
    sprintf(dash, "\u2501");
    string line;

    for (int x = 0; x < length; ++x)
        line += dash;

    return line;
}


