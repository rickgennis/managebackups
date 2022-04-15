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
#include "math.h"
#include <sys/types.h>
#include <pwd.h>
#include <vector>

#include "pcre++.h"
#include "util_generic.h"
#include "globals.h"
#include "PipeExec.h"


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


vector<string> perlSplit(string regex, string haystack) {
    Pcre theRE("(" + regex + ")", "g");
    vector<string> result;

    size_t pos = 0;
    size_t dataStart = 0;
    size_t dataEnd = 0;

    while (pos <= haystack.length() && theRE.search(haystack, pos)) {
        pos = theRE.get_match_end(0);
        string delimiter = theRE.get_match(0);

        dataEnd = ++pos - delimiter.length() + 1;
        result.insert(result.end(), string(haystack, dataStart, dataEnd - dataStart - 1));
        dataStart = pos; 
    }

    result.insert(result.end(), string(haystack, dataStart, string::npos));
    return result;
}


void log(string message) {
#ifdef __APPLE__
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
        logFile << string(timeStamp) << "[" << to_string(GLOBALS.pid) << "] " << message << endl;
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


string approximate(double size, int maxUnits, bool commas) {
    int index = 0;
    char unit[] = {'B', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};

    while (size >= 1024 && 
            (maxUnits < 0 || index < maxUnits) &&
            index++ <= sizeof(unit)) { 
        size /= 1024;
    }

    char buffer[150];
    sprintf(buffer, index > 2 ? "%.01f" : "%.0f", index > 2 ? size : floor(size));
    string unitSuffix(1, unit[index]);
    string result = string(buffer);

    if (commas) {
        string finalResult;
        auto numLen = result.length();
        auto commaOffset = numLen % 3;

        for (auto i = 0; i < numLen; ++i) {
            if (i % 3 == commaOffset && i)
                finalResult += ',';

            finalResult += result[i];
        }

        result = finalResult;
    }

    return(result + (index ? unitSuffix : ""));
}


unsigned long approx2bytes(string approx) {
    vector<string> units = { "B", "K", "M", "G", "T", "P", "E", "Z", "Y" };
    Pcre reg("^((?:\\d|\\.)+)\\s*(?:(\\w)(?:[Bb]$|$)|$)");

    if (reg.search(approx)) {

        // was a numeric value and a unit specified?
        if (reg.matches() > 1) {

            // save the two pieces
            auto numericVal = stof(reg.get_match(0));
            string unit = reg.get_match(1);

            // capitalize the units to match the lookup in the vector
            for (auto &c: unit) c = toupper(c);

            // lookup the supplied unit in the vector
            auto it = find(units.begin(), units.end(), unit);

            if (it != units.end()) {
                // determine its index and calculate the result
                int index = it - units.begin();
                return floor(pow(1024, index) * numericVal);
            }
        }
        // was just a numeric value specified?
        else if (reg.matches() > 0) {
            return floor(stof(reg.get_match(0)));
        }
    }

    throw std::runtime_error("error parsing size from " + approx);
}


string seconds2hms(unsigned long seconds) {
    string result;
    int unit[] = {3600, 60, 1};
    bool dataAdded = false;

    if (seconds >= 60*60*100)
        return(string("> ") + to_string(int(seconds / (60 * 60 * 24))) + " days");

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
            //result += dataAdded ? ":00" : index == (sizeof(unit) / sizeof(unit[0])) - 1 ? "  " : "   ";
            result += dataAdded ? ":00" : index == (sizeof(unit) / sizeof(unit[0])) - 1 ? "00" : "00:";
        }
    }

    return(result.length() ? result : "        ");
}


string timeDiff(struct timeval start, struct timeval end, int maxUnits, int precision) {
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
        sprintf(ms, string(string("%.") + to_string(precision) + "f").c_str(), 1.0 * us / MILLION);

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
                
            // then replace the parameter with a wildcard expansion of the matching files
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
    for (auto &file: fileList)
        if (file.length() > 2 && file.substr(0, 2) == "./")
            file.erase(0, 2);

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
    vector<string> parts;

    // if a path is specified try it
    if (app.find("/") != string::npos) {
        if (!access(app.c_str(), X_OK))
            return(app);
    }

    // grab the binary name as the last delimited element given
    vector<string> appParts;
    stringstream appTokenizer(app);
    while (getline(appTokenizer, tempStr, '/'))
        appParts.push_back(tempStr);
    auto appBinary = appParts.back();

    // parse the PATH
    stringstream pathTokenizer(path);
    while (getline(pathTokenizer, tempStr, ':'))
        parts.push_back(tempStr);
        
    // try to find the binary in each component of the path
    for (auto piece: parts) {
        string binary = string(piece) + "/" + appBinary;
        if (!access(binary.c_str(), X_OK))
        return binary;
    }

    // give up
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


void sendEmail(string from, string recipients, string subject, string message) {
    string headers = "X-Mailer: Apple Sendmail\nContent-Type: text/plain\nReturn-Path: " + from + "\nSubject: " + subject + "\n\n";
    string bin = locateBinary("/usr/sbin/sendmail");

    if (bin.length())
        bin += " -f " + from + " " + recipients;
    else {
        bin = locateBinary("mail") + " -s \"" + subject + "\" " + recipients;
        headers = "";
    }

    PipeExec mail(bin);
    mail.execute();

    if (headers.length())
        mail.writeProc(headers.c_str(), headers.length());

    mail.writeProc(message.c_str(), message.length());
    mail.closeWrite();
}


string todayString() {
    char text[100];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(text, sizeof(text)-1, "%c", t);
    return(text);
}


string blockp(string data, int width) {
    char cstr[2000];
    sprintf(cstr, string(string("%") + to_string(width) + "s").c_str(), data.c_str());
    return(cstr);
}



string catdir(string dir) {
    string result;
    DIR *c_dir;
    struct dirent *c_dirEntry;

    if ((c_dir = opendir(dir.c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                continue;

            ifstream aFile;
            aFile.open(dir + "/" + c_dirEntry->d_name);

            if (aFile.is_open()) {
                string data;

                while (getline(aFile, data)) 
                    result += data + "\n";

                aFile.close();
            }

        }
        closedir(c_dir);
    }

    size_t p;
    while ((p = result.find("\r\n")) != string::npos)
        result.erase(p, 1);

    while ((p = result.find("\n\n")) != string::npos)
        result.erase(p, 1);

    if (result.back() == '\n')
        result.pop_back();

    return(result);
}


// delete an absolute directory (rm -rf).
// wildcards are not interpreted in the directory name.
void rmrfdir(string dir) {
    DIR *c_dir;
    struct dirent *c_dirEntry;

    if ((c_dir = opendir(dir.c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                continue;

            struct stat statData;
            string filename = addSlash(dir) + c_dirEntry->d_name; 
            if (!stat(filename.c_str(), &statData)) {

                // recurse into subdirectories
                if ((statData.st_mode & S_IFMT) == S_IFDIR) 
                    rmrfdir(filename);
                else
                    unlink(filename.c_str());
            }
        }

        closedir(c_dir);
        rmdir(dir.c_str());
    }
}


