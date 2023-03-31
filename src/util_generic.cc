#include <iostream>
#include <sstream>
#include <fstream>
#include <dirent.h>
#include <unistd.h>
#include "time.h"
#include <syslog.h>
#include <openssl/evp.h>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <utime.h>
#include <pwd.h>
#include <vector>
#include <set>

#include "pcre++.h"
#include "util_generic.h"
#include "globals.h"
#include "ipc.h"

using namespace pcrepp;

#if defined(__linux__)
#  include <endian.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  include <sys/endian.h>
#elif defined(__OpenBSD__)
#  include <sys/types.h>
#  define be16toh(x) betoh16(x)
#  define be32toh(x) betoh32(x)
#  define be64toh(x) betoh64(x)
#endif



string s(int number) {
    return(number == 1 ? "" : "s");
}


string ies(int number) {
    return(number == 1 ? "y" : "ies");
}

string plural(size_t number, string text) {
    return (to_string(number) + " " + text + (number == 1 ? "" : "s"));
}

string plurali(size_t number, string text) {
    return (to_string(number) + " " + text + (number == 1 ? "y" : "ies"));
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

    while (pos <= haystack.length() && theRE.search(haystack, (int)pos)) {
        pos = theRE.get_match_end(0);
        string delimiter = theRE.get_match(0);

        dataEnd = ++pos - delimiter.length() + 1;
        result.insert(result.end(), string(haystack, dataStart, dataEnd - dataStart - 1));
        dataStart = pos; 
    }

    result.insert(result.end(), string(haystack, dataStart, string::npos));
    return result;
}


string log(string message) {
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
    
    return message;
}


struct timeval mktimeval(time_t secs) { struct timeval t; t.tv_sec = secs; t.tv_usec = 0; return t; }


string slashConcat(string str1, string str2) {
    if (str1.length() && str1[str1.length() - 1] == '/')
        str1.pop_back();

    if (str2.length() && str2[0] == '/')
        str2.erase(0, 1);

    return(str1 + "/" + str2);
}


string slashConcat(string str1, string str2, string str3) {
    return slashConcat(slashConcat(str1, str2), str3);
}


s_pathSplit pathSplit(string path) {
    s_pathSplit s;
    
    if (path.length() > 1) {
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
    }
    else {
        if (path.length()) {
            if (path[0] == '/') {
                s.dir = "/";
                s.file = "";
            }
            else {
                s.dir = ".";
                s.file = path;
            }
        }
        else {
            s.dir = "";
            s.file = "";
        }
    }

    return s;
}


string MD5file(string filename, bool quiet, string reason) {
    FILE *inputFile;

    if ((inputFile = fopen(filename.c_str(), "rb")) != NULL) {
        string message = "MD5 " + filename + (reason.length() ? " " + reason : "...");

        if (!quiet)
            cout << message << flush;

        unsigned char data[65536];
        unsigned long bytesRead;
        EVP_MD_CTX *md5Context;
        unsigned char *md5Digest;
        unsigned int md5DigestLen = EVP_MD_size(EVP_md5());

        md5Context = EVP_MD_CTX_new();
        EVP_DigestInit_ex(md5Context, EVP_md5(), NULL);

        while ((bytesRead = fread(data, 1, 65536, inputFile)) != 0)
            EVP_DigestUpdate(md5Context, data, bytesRead);

        fclose(inputFile);
        md5Digest = (unsigned char*)OPENSSL_malloc(md5DigestLen);
        EVP_DigestFinal_ex(md5Context, md5Digest, &md5DigestLen);
        EVP_MD_CTX_free(md5Context);

        char tempStr[md5DigestLen * 2 + 1];
        for (int i = 0; i < md5DigestLen; i++)
            snprintf(tempStr+(2*i), 3, "%02x", md5Digest[i]);
        tempStr[md5DigestLen * 2] = 0;

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
        EVP_MD_CTX *md5Context;
        unsigned char *md5Digest;
        unsigned int md5DigestLen = EVP_MD_size(EVP_md5());
        char* c_origString = const_cast<char*>(origString.c_str());

        md5Context = EVP_MD_CTX_new();
        EVP_DigestInit_ex(md5Context, EVP_md5(), NULL);

        EVP_DigestUpdate(md5Context, c_origString, origString.length());

        md5Digest = (unsigned char*)OPENSSL_malloc(md5DigestLen);
        EVP_DigestFinal_ex(md5Context, md5Digest, &md5DigestLen);
        EVP_MD_CTX_free(md5Context);

        char tempStr[md5DigestLen * 2];
        for (int i = 0; i < md5DigestLen ; i++)
            snprintf(tempStr+(2*i), 3, "%02x", md5Digest[i]);

        return(tempStr);
}


string onevarsprintf(string format, string data) {
    char buffer[1000];
    snprintf(buffer, sizeof(buffer), format.c_str(), data.c_str());
    return buffer;
}


string approximate(size_t size, int maxUnits, bool commas) {
    int index = 0;
    long double decimalSize = size;
    char unit[] = {'B', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};

    while (decimalSize >= 1024 &&
            (maxUnits < 0 || index < maxUnits) &&
            index++ <= sizeof(unit)) {
        decimalSize /= 1024.0;
    }

    char buffer[150];
    snprintf(buffer, sizeof(buffer), index > 1 ? "%.01Lf" : "%.0Lf", index > 1 ? decimalSize : floor(decimalSize));
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


size_t approx2bytes(string approx) {
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
                auto index = it - units.begin();
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


string seconds2hms(time_t seconds) {
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
            snprintf(buffer, sizeof(buffer), "%02.0f", value);
            result += string(dataAdded ? ":" : "") + buffer;
            dataAdded = true;
        }
        else
            result += dataAdded ? ":00" : index == (sizeof(unit) / sizeof(unit[0])) - 1 ? "00" : "00:";
    }

    return(result.length() ? result : "        ");
}


string timeDiffSingle(struct timeval duration, int maxUnits, int precision) {
    auto secs = duration.tv_sec;
    auto us = duration.tv_usec;
    auto offset = secs;
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
            unsigned long value = floor(offset / unit_it->first);
            unsigned long leftover = offset % unit_it->first;

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
        snprintf(ms, sizeof(ms), string(string("%.") + to_string(precision) + "f").c_str(), 1.0 * us / MILLION);

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


string timeDiff(struct timeval start, struct timeval end, int maxUnits, int precision) {
    struct timeval diffTime;
    mytimersub(&end, &start, &diffTime);

    return timeDiffSingle(diffTime, maxUnits, precision);
}


string dw(int which) {
    const char* DOW[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    return(DOW[which]);
}


void setFilePerms(string filename, struct stat &statData, bool exitOnError) {
    if (lchmod(filename.c_str(), statData.st_mode)) {
        SCREENERR(log("error: unable to chmod " + filename + " - " + strerror(errno)));
        if (exitOnError)
            exit(1);
    }
    
    if (lchown(filename.c_str(), statData.st_uid, statData.st_gid)){
        SCREENERR(log("error: unable to chown " + filename + " - " + strerror(errno)));
        if (exitOnError)
            exit(1);
    }
    
    struct timeval tv[2];
    tv[0].tv_sec  = tv[1].tv_sec  = statData.st_mtime;
    tv[0].tv_usec = tv[1].tv_usec = 0;
    if (lutimes(filename.c_str(), tv)) {
        SCREENERR(log("error: unable to set utime on " + filename + " - " + strerror(errno)));
        if (exitOnError)
            exit(1);
    }
}


int mkdirp(string dir, mode_t mode) {
    struct stat statBuf;
    int result = 0;

    if (stat(dir.c_str(), &statBuf) == -1) {
        char data[PATH_MAX];
        strcpy(data, dir.c_str());
        char *p = strtok(data, "/");
        string path;

        while (p) {
            path += string("/") + p;   

            if (stat(path.c_str(), &statBuf) == -1)
               result = mkdir(path.c_str(), mode);

            if (result)
                return(result);

            p = strtok(NULL, "/");
        }
    }

    return 0;
}


void mkdirp(string dir, struct stat &statData) {
    mkdirp(dir);
    setFilePerms(dir, statData);
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
    int pos = 0;
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
    vector<string> subDirs;
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
                    if (!lstat(string(baseDir + "/" + c_dirEntry->d_name).c_str(), &statData)) {

                        // if this matching dir entry is a subdirectory, add it to the list to call recursively
                        if (S_ISDIR(statData.st_mode))
                            subDirs.insert(subDirs.end(), c_dirEntry->d_name);
                        else {
                            // if it's not a subdirectory just add this entry to the result list
                            result.insert(result.end(), resultBaseDir + (resultBaseDir.back() == '/' ? "" : "/") + c_dirEntry->d_name);
                        }
                    }
                    else
                        log("expandWildcard: unable to stat " + baseDir + "/" + c_dirEntry->d_name + " - " + strerror(errno));
                }
            }
            closedir(c_dir);

            for (auto &dir: subDirs) {
                auto subResult = expandWildcardSub(fileSpec, baseDir + (baseDir.back() == '/' ? "" : "/") + dir, index+1);
                result.insert(result.end(), subResult.begin(), subResult.end());
            }
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
    snprintf(dash, sizeof(dash), "\u2501");
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
        mail.ipcWrite(headers.c_str(), headers.length());

    mail.ipcWrite(message.c_str(), message.length());
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
    snprintf(cstr, sizeof(cstr), string(string("%") + to_string(width) + "s").c_str(), data.c_str());
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
// wildcards are not interpreted in the directory name (use expandWildcardFilespec()).
bool rmrf(string item) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    struct stat statData;
    vector<string> subDirs;

    if (!lstat(item.c_str(), &statData)) {
        if (S_ISDIR(statData.st_mode)) {
            
            if ((c_dir = opendir(item.c_str())) != NULL) {
                while ((c_dirEntry = readdir(c_dir)) != NULL) {
                    if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                        continue;
                    
                    string filename = slashConcat(item, c_dirEntry->d_name);
                    if (!lstat(filename.c_str(), &statData)) {
                        
                        // save directories in a list to do after we close this fd
                        if (S_ISDIR(statData.st_mode))
                            subDirs.insert(subDirs.end(), filename);
                        else
                            if (unlink(filename.c_str())) {
                                closedir(c_dir);
                                log("error: unable to remove " + filename + " - " + strerror(errno));
                                return false;
                            }
                    }
                }
                closedir(c_dir);
                
                // recurse into subdirectories
                for (auto &dir: subDirs)
                    if (!rmrf(dir))
                        return false;

                // remove this directory itself
                if (rmdir(item.c_str())) {
                    log("error: unable to remove " + item + " - " + strerror(errno));
                    return false;
                }
            }
        }
        else
            if (unlink(item.c_str())) {
                log("error: unable to remove " + item + " - " + strerror(errno));
                return false;
            }
    }
    
    return true;
}


int mkbasedirs(string path) {
    if (path.length())
        return mkdirp(path.substr(0, path.find_last_of("/")));

    return -1;
}

DiskStats dus(string path) {    // du -s
    set<ino_t> seenInodes;
    set<ino_t> newInodes;
    return dus(path, seenInodes, newInodes);
}


DiskStats dus(string path, set<ino_t>& seenInodes, set<ino_t>& newInodes) {    // du -s
    vector<string> subDirs;
    DIR *dir;
    struct stat statData;
    struct dirent *dirEnt;
    DiskStats ds;
    
    if ((dir = opendir(path.c_str())) == NULL) {
        perror(path.c_str());
        return DiskStats(0, 0, 0, 0);
    }

    while ((dirEnt = readdir(dir)) != NULL) {
        if (!strcmp(dirEnt->d_name, ".") || !strcmp(dirEnt->d_name, ".."))
            continue;
        
        string fullFilename = path + "/" + dirEnt->d_name;
        ++GLOBALS.statsCount;
        if (lstat(fullFilename.c_str(), &statData) < 0)
            log("error: stat(" + fullFilename + "): " + strerror(errno));
        else {
            if (seenInodes.find(statData.st_ino) != seenInodes.end() ||
                newInodes.find(statData.st_ino) != newInodes.end()) {
                ds.savedInBytes += statData.st_size;
                ds.savedInBlocks += 512 * statData.st_blocks;
            }
            else {
                ds.sizeInBytes += statData.st_size;
                ds.sizeInBlocks += 512 * statData.st_blocks;
            }
            
            newInodes.insert(statData.st_ino);
            
            if (S_ISDIR(statData.st_mode))
                subDirs.insert(subDirs.end(), fullFilename);
        }
    }
    closedir(dir);

    for (auto &subDir: subDirs)
        ds += dus(subDir, seenInodes, newInodes);
            
    return ds;
}


// convenience function to consolidate printing screen errors, logging and returning
// to the notify() function
string errorcom(string profile, string message) {
    SCREENERR("\t• " << profile << " " << message);
    log(profile + " " + message);
    return("\t• " + message);
}



int simpleSelect(int rFd, int wFd, int timeoutSecs) {
    fd_set dataSet;
    FD_ZERO(&dataSet);

    fd_set errorSet;
    FD_ZERO(&errorSet);

    if (rFd) {
        FD_SET(rFd, &dataSet);
        FD_SET(rFd, &errorSet);
    }

    if (wFd) {
        FD_SET(wFd, &dataSet);
        FD_SET(wFd, &errorSet);
    }

    struct timeval tv;
    tv.tv_sec = timeoutSecs;
    tv.tv_usec = 0;

    return select(max(rFd, wFd) + 1, rFd ? &dataSet : NULL, wFd ? &dataSet : NULL, &dataSet, &tv);
}


void showError(string message) {
    SCREENERR(message + "\n");
    log(message);
}


int copyFile(string srcFile, string destFile) {
    std::ifstream inF(srcFile, ios_base::in | ios_base::binary);
    if (!inF) return 0;

    std::ofstream outF(destFile, ios_base::out | ios_base::binary);
    if (!outF) return 0;

    char buffer[32 * 1024];
    do {
        inF.read(buffer, sizeof(buffer));
        outF.write(buffer, inF.gcount());
    } while (inF.gcount() > 0);

    inF.close();
    outF.close();
        
    return 1;
}


string ue(string file) {
    file.erase(remove(file.begin(), file.end(), '\\'), file.end());
    return file;
}


bool exists(const std::string& name) {
    struct stat statBuffer;
    ++GLOBALS.statsCount;
    return (stat(name.c_str(), &statBuffer) == 0);
}


string getUserHomeDir(int uid) {
    char *homeDir;
    
    if ((homeDir = getenv("HOME")) != NULL)
        return homeDir;
    
    auto h = getpwuid(uid == -1 ? getuid() : uid);
    if (h != NULL)
        return h->pw_dir;

    return "";
}


int getUidFromName(string userName) {
    if (!userName.length())
        return getuid();
    
    struct passwd* pwd;
    pwd = getpwnam(userName.c_str());
    if (pwd == NULL)
        return -1;
    
    return pwd->pw_uid;
}


time_t filename2Mtime(string filename) {
    Pcre dateRE = Pcre(DATE_REGEX);
    int date_year, date_month, date_day, time_hour = 0, time_min = 0, time_sec = 0;

    if (dateRE.search(filename) && dateRE.matches() > 2) {
        date_year  = stoi(dateRE.get_match(0));
        date_month = stoi(dateRE.get_match(1));
        date_day   = stoi(dateRE.get_match(2));
        
        if (dateRE.matches() > 5) {
            time_hour = stoi(dateRE.get_match(3));
            time_min  = stoi(dateRE.get_match(4));
            time_sec  = stoi(dateRE.get_match(5));
        }
    }
    else
        return 0;

    struct tm fileTime;
    fileTime.tm_sec  = time_sec;
    fileTime.tm_min  = time_min;
    fileTime.tm_hour = time_hour;
    fileTime.tm_mday = date_day;
    fileTime.tm_mon  = date_month - 1;
    fileTime.tm_year = date_year - 1900;
    fileTime.tm_isdst = -1;

    return mktime(&fileTime);
}


int forkMvCmd(string oldDir, string newDir) {
    // actually move the backup files.  this is tempting to do internally
    // but if you look at the source to the mv command there are more one-
    // off special cases than you can count.  may as well let it do what
    // it's good at.
    auto mv = locateBinary("/bin/mv");
    
    auto pid = fork();
    if (pid < 0) {
        SCREENERR(string("error: unable to execute fork to run mv - ") + strerror(errno));
        exit(1);
    }
    
    if (pid) {
        int status;
        
        pid = wait(&status);
        if (WIFEXITED(status))
            return status;
        
        return -1;
    }

    execl(mv.c_str(), mv.c_str(), oldDir.c_str(), newDir.c_str(), 0);
    exit(0);
}


string realpathcpp(string origPath) {
    char tmpBuf[PATH_MAX+1];
    return (realpath(origPath.c_str(), tmpBuf) == NULL ? "" : tmpBuf);
}


char getFilesystemEntryType(mode_t mode) {
    char c = '?';
    
    if (S_ISREG(mode))
        c = '-';
    else
        if (S_ISDIR(mode))
            c = 'd';
    else
        if (S_ISLNK(mode))
            c = 'l';
    else
        if (S_ISSOCK(mode))
            c = 's';
    else
        if (S_ISCHR(mode))
            c = 'c';
    else
        if (S_ISBLK(mode))
            c = 'b';
    else
        if (S_ISFIFO(mode))
            c = 'p';
    
    return c;
}


char getFilesystemEntryType(string entry) {
    struct stat statData;
    
    if (!stat(entry.c_str(), &statData))
        return getFilesystemEntryType(statData.st_mode);
    
    log("error: unable to stat " + entry + " - " + strerror(errno));
    
    return '?';
}


void processDirectory(string directory, string pattern, bool exclude, void (*processor)(processorFileData&), void *passData, int maxDepth, string internalUseDir) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    vector<string> subDirs;
    Pcre patternRE(pattern);
    processorFileData file;
    file.dataPtr = passData;
    
    if (internalUseDir.length())
        file.origDir = internalUseDir;
    else
        file.origDir = directory;

    if (!maxDepth)
        return;
    
    if ((c_dir = opendir(ue(directory).c_str())) != NULL) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {

            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                continue;

            file.filename = slashConcat(directory, c_dirEntry->d_name);
            
            if (!stat(file.filename.c_str(), &file.statData)) {
                
                // process directories
                if (S_ISDIR(file.statData.st_mode)) {
                    subDirs.insert(subDirs.end(), file.filename);
                    processor(file);
                }
                else {
                    // filter for patterns
                    if (pattern.length()) {
                        bool found = patternRE.search(file.filename);
                        
                        if (exclude && found)
                            continue;
                        
                        if (!exclude && !found)
                            continue;
                    }
                    
                    // process regular files
                    processor(file);
                }
            }
        }
        
        closedir(c_dir);
    }
    
    for (auto &dir: subDirs)
        processDirectory(dir, pattern, exclude, processor, passData, maxDepth > -1 ? maxDepth - 1 : -1, file.origDir);
}



string progressPercentage(int totalIterations, int totalSteps,
                    int iterationsComplete, int stepsComplete, string detail) {
    static int prevLength = 0;
    string result;
    
    // start with backspaces to erase our previous message
    if (totalIterations < 0)
        prevLength = 0;
    else
        if (prevLength)
            result = string(prevLength, '\b') + string(prevLength, ' ') + string(prevLength, '\b');
    
    // numFS = 0 can be used to just backspace over the last status and blank it out
    if (totalIterations > 0) {
        int target = totalIterations * totalSteps;
        int current = iterationsComplete * totalSteps + stepsComplete;
        string currentProgress = to_string(int((float)current / (float)target * 100)) + "% " + detail + " ";
        prevLength = (int)currentProgress.length();
        result += currentProgress;
    }

    return result;
}
