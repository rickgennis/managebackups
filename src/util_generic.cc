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
#include <list>
#include <set>

#include "pcre++.h"
#include "util_generic.h"
#include "globals.h"
#include "ipc.h"
#include "exception.h"

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


string perlJoin(string delimiter, vector<string> items) {
    string result;
    
    for (auto &item: items)
        result += (result.length() ? delimiter : "") + item;
    
    return result;
}

// splitOnRegex: the RE given matches on the tokens to be returned (the data)
// perlSplit: the RE given matches the delimiter and tokens are found in between
vector<string> perlSplit(string regex, string haystack) {
    Pcre theRE("(" + regex + ")", "g");
    vector<string> result;
    
    size_t pos = 0;
    size_t dataStart = 0;
    size_t dataEnd = 0;
    
    while (pos <= haystack.length() && theRE.search(haystack, (int)pos)) {
        pos = theRE.get_match_end(0);
        string delimiter = theRE.get_match(0);
        dataEnd = pos++ - delimiter.length();
        
        result.insert(result.end(), string(haystack, dataStart, dataEnd - dataStart + 1));
        
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
        logFile << string(timeStamp) << "[" << to_string(GLOBALS.pid) << "] " << commafy(message) << endl;
        logFile.close();
    }
#else
    syslog(LOG_CRIT, "%s", commafy(message).c_str());
#endif
    
    return message;
}


struct timeval mktimeval(time_t secs) { struct timeval t; t.tv_sec = secs; t.tv_usec = 0; return t; }


string slashConcat(string str1, string str2, string str3) {
    if (str1.length() && str1[str1.length() - 1] == '/')
        str1.pop_back();
    
    if (str2.length() && str2[0] == '/')
        str2.erase(0, 1);
    
    return (str3.length() ? slashConcat(str1 + "/" + str2, str3) : str1 + "/" + str2);
}


s_pathSplit pathSplit(string path) {
    s_pathSplit s;
    s.dir = s.file = s.file_ext = s.file_base = "";
    
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
        
        pos = s.file.rfind(".");
        
        if (pos == string::npos)
            s.file_base = s.file;
        else {
            s.file_base = s.file.substr(0, pos);
            
            if (pos < s.file.length() - pos)
                s.file_ext = s.file.substr(pos + 1);
        }
    }
    else {
        if (path.length()) {
            if (path[0] == '/')
                s.dir = "/";
            else {
                s.dir = ".";
                s.file = path;
                
                if (path[0] != '.')
                    s.file_base = path;
            }
        }
    }
    
    return s;
}


string getDirUp(string path) { return pathSplit(path).dir; }


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


string approximate(size_t size, int maxUnits, bool commas, bool base10) {
    int index = 0;
    long double decimalSize = size;
    char unit[] = {'B', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};
    
    while ((decimalSize >= (base10 ? 1000 : 1024)) &&
           (maxUnits < 0 || index < maxUnits) &&
           index++ <= sizeof(unit)) {
        decimalSize /= (base10 ? 1000 : 1024.0);
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


// return the printable length of a string, ignoring ANSI escape codes (i.e. colors)
unsigned int ansistrlength(string source) {
    auto sourceLength = source.length();
    unsigned int resultLength = 0;
    bool shortEsc = false;
    bool longEsc = false;
    
    for (unsigned int i = 0; i < sourceLength; ++i) {
        
        // start of multi-character escape sequence
        if (shortEsc && !longEsc && source[i] == '[')
            longEsc = true;
        
        // start of single character escape sequence
        if (source[i] == '\x1b')
            shortEsc = true;
        
        if (!shortEsc && !longEsc)
            ++resultLength;
        
        // end of single character escape sequence
        if (shortEsc && !longEsc && source[i] != '\x1b')
            shortEsc = false;
        
        // end of multi-character escape sequence
        if (longEsc && source[i] != '[' && source[i] >= '@' && source[i] <= '~')
            shortEsc = longEsc = false;
    }
    
    return resultLength;
}


size_t approx2bytes(string approx) {
    vector<string> units = { "B", "K", "M", "G", "T", "P", "E", "Z", "Y" };
    Pcre reg("^((?:\\d|\\.)+)\\s*(?:(\\w)(?:[Bb]$|$)|$)");
    
    if (approx.length()) {
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
            else
                if (reg.matches() > 0) {
                    return floor(stof(reg.get_match(0)));
            }
        }
    }
    else
        return 0;
    
    throw std::runtime_error("approx2bytes - error parsing size from string '" + approx + "'");
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
        char decimalSecs[50];
        snprintf(decimalSecs, sizeof(decimalSecs), string(string("%.") + to_string(precision) + "f").c_str(), 1.0 * us / MILLION);
                
        string sms = decimalSecs;
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
        
        if (secs < 1)
            result += string(" (" + to_string(us) + " μs)");
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
    if (!S_ISLNK(statData.st_mode))
        if (chmod(filename.c_str(), statData.st_mode)) {
            SCREENERR(log("error: unable to chmod " + filename + " - " + strerror(errno)));
            if (exitOnError)
                exit(1);
        }
    
    if (lchown(filename.c_str(), statData.st_uid, statData.st_gid)){
        SCREENERR(log("error: unable to chown " + filename + " - " + strerror(errno)));
        if (exitOnError)
            exit(1);
    }
    
    struct timespec tv[2];
#ifdef __APPLE__
    tv[0].tv_sec  = statData.st_atimespec.tv_sec;
    tv[0].tv_nsec  = statData.st_atimespec.tv_nsec;
    tv[1].tv_sec  = statData.st_mtimespec.tv_sec;
    tv[1].tv_nsec  = statData.st_mtimespec.tv_nsec;
#else
    tv[0].tv_sec  = statData.st_atim.tv_sec;
    tv[0].tv_nsec  = statData.st_atim.tv_nsec;
    tv[1].tv_sec  = statData.st_mtim.tv_sec;
    tv[1].tv_nsec  = statData.st_mtim.tv_nsec;
#endif
    
    if (utimensat(0, filename.c_str(), tv, 0)) {
        SCREENERR(log("error: unable to set time on " + filename + " - " + strerror(errno)));
        if (exitOnError)
            exit(1);
    }
}


int mkdirp(string dir, mode_t mode) {
    struct stat statBuf;
    int result = 0;
    
    if (mystat(dir, &statBuf) == -1) {
        char data[PATH_MAX + 1];
        strcpy(data, dir.c_str());
        char *p = strtok(data, "/");
        string path;
        
        while (p) {
            path += string("/") + p;
            
            if (mystat(path, &statBuf) == -1)
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


string trimQuotes(string s, bool unEscape) {
    Pcre regA("^([\'\"]+)");
    string result = s;
    
    if (regA.search(s) && regA.matches()) {
        string openQuotes = regA.get_match(0);
        string closeQuotes = openQuotes;
        reverse(closeQuotes.begin(), closeQuotes.end());
        
        Pcre regB("^" + openQuotes + "(.*)" + closeQuotes + "$");
        if (regB.search(s) && regB.matches())
            result = regB.get_match(0);
    }
    
    if (unEscape) {
        size_t altpos;  // remove any remaining backslashes
        while ((altpos = result.find("\\")) != string::npos)
            result.erase(altpos, 1);
    }
    
    return result;
}

// splitOnRegex: the RE given matches on the tokens to be returned (the data)
// perlSplit: the RE given matches the delimiter and tokens are found in between
void splitOnRegex(vector<string>& result, string data, Pcre& re, bool trimQ, bool unEscape) {
    Pcre regex(re);
    string temp;
    int pos = 0;
    
    while (pos <= data.length() && regex.search(data, pos)) {
        pos = regex.get_match_end(0);
        ++pos;
        temp = regex.get_match(0);
        
        if (unEscape) {
            size_t altpos;  // remove any remaining backslashes
            while ((altpos = temp.find("\\")) != string::npos)
                temp.erase(altpos, 1);
        }
        
        result.push_back(trimQ ? trimQuotes(temp) : temp);
    }
}


// split a string into a vector on pipes, except where quoted or escaped
vector<string> string2vectorOnPipe(string data, bool trimQ, bool unEscape) {
//  Pcre regex("((?:([\'\"])(?:(?!\\g2).|(?:(?<=\\\\)[\'\"]))+(?<!\\\\)\\g2)|(?:\\||(?:(?<=\\\\)\\|))+)", "g");
    Pcre regex("((?:[^\'\"|]*([\'\"]).+?(?<!\\\\)\\g2[^\'\"|]*)|(?:[^|]|(?:(?<=\\\\)\\|))+)", "g");
    vector<string> result;
    splitOnRegex(result, data, regex, trimQ, unEscape);
    return result;
}


// split a string into a vector on spaces, except where quoted or escaped
vector<string> string2vectorOnSpace(string data, bool trimQ, bool unEscape) {
//  Pcre regex("((?:([\'\"])(?:(?!\\g2).|(?:(?<=\\\\)[\'\"]))+(?<!\\\\)\\g2)|(?:\\S|(?:(?<=\\\\)\\s))+)", "g");
    Pcre regex("((?:([\'\"]).+?(?<!\\\\)\\g2)|(?:\\S|(?:(?<=\\\\)\\s))+)", "g");
    vector<string> result;
    splitOnRegex(result, data, regex, trimQ, unEscape);
    return result;
}

//"((?:[^\'\"|]*([\'\"]).+?(?<!\\\\)\\g2[^\'\"|]*)|(?:[^|]|(?:(?<=\\\\)\\|))+)"
//         "((?:([\'\"]).+?(?<!\\\\)\\g2)|(?:\\S|(?:(?<=\\\\)\\s))+)"

int varexec(string fullCommand) {
    char *params[400];
    string match;
    int index = 0;
    
    // don't let string2vector remove quotes (i.e. cleanup) because we want
    // to check for quoted wildcards first
    auto tokens = string2vectorOnSpace(fullCommand, false, false);
    
    for (auto &token: tokens) {
        /* token contains wildcard, add the matching files instead */
        if (token.length() &&   // if its a valid string
            // not quoted
            !((token.front() == '\'' || token.front() == '\"') && token.front() == token.back()) &&
            // and has shell wildcards
            (token.find("*") != string::npos || token.find("?") != string::npos)) {
            
            // then replace the parameter with a wildcard expansion of the matching files
            auto files = expandWildcardFilespec(trimQuotes(token));
            
            for (auto entry: files) {
                // no need to free() this because we're going to exec()
                params[index] = (char *)malloc(entry.length() + 1);
                
                // add each matching file as a parameter for exec()
                strcpy(params[index++], entry.c_str());
            }
        }
        
        /* token doesn't contain a wildcard, just add the item */
        else {
            size_t altpos;
            // escaping is handled above by cmdRE and is complete by the time we get here
            // so now we remove any remaining backslashes in the string
            while ((altpos = token.find("\\")) != string::npos)
                token.erase(altpos, 1);
            
            params[index] = (char *)malloc(token.length() + 1);
            
            // and drop any quotes that are on the ends
            strcpy(params[index++], trimQuotes(token).c_str());
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
                    if (!mylstat(baseDir + "/" + c_dirEntry->d_name, &statData)) {
                        
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
    Pcre regTrue("(^\\s*(t|true|y|yes|1)\\s*$)|(^\\s*$)", "i");
    // a blank value (^\\s*$) is parsed as true to support someone writing just the directive name in the config file.
    // e.g. these two lines would be interpreted identically:
    //      default: true
    //      default
    
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


bool catdirCallback(pdCallbackData &file) {
    ifstream aFile;
    
    aFile.open(file.filename);
    if (aFile.is_open()) {
        string data;
        
        while (getline(aFile, data))
            *(string*)(file.dataPtr) += data + "\n";
        
        aFile.close();
    }
    
    return true;
}


string catdir(string dir) {
    string result;
    
    processDirectory(dir, "", false, false, catdirCallback, &result);
    
    size_t p;
    while ((p = result.find("\r\n")) != string::npos)
        result.erase(p, 1);
    
    while ((p = result.find("\n\n")) != string::npos)
        result.erase(p, 1);
    
    if (result.back() == '\n')
        result.pop_back();
    
    return result;
}


bool rmrfCallback(pdCallbackData &file) {
    return (S_ISDIR(file.statData.st_mode) ? !rmdir(file.filename.c_str()) : !unlink(file.filename.c_str()));
}


// delete an absolute directory (rm -rf).
// wildcards are not interpreted in the directory name (use expandWildcardFilespec()).
bool rmrf(string directory, bool includeTopDir) {
    return (processDirectory(directory, "", false, false, rmrfCallback, NULL, -1, includeTopDir, false) == "");
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


struct dsDataType {
    DiskStats *ds;
    set<ino_t> *seenI;
    set<ino_t> *newI;
};


bool dsCallback(pdCallbackData &file) {
    dsDataType *data = (dsDataType*)file.dataPtr;
    
    if (S_ISDIR(file.statData.st_mode))
        ++data->ds->dirs;
    else
        if (S_ISLNK(file.statData.st_mode))
            ++data->ds->symLinks;
        else {
            if (data->seenI->find(file.statData.st_ino) == data->seenI->end() &&
                data->newI->find(file.statData.st_ino) == data->newI->end()) {
                data->ds->usedInBytes += file.statData.st_size;
                data->ds->usedInBlocks += 512 * file.statData.st_blocks;
                ++data->ds->mods;
            }
            else {
                data->ds->savedInBytes += file.statData.st_size;
                data->ds->savedInBlocks += 512 * file.statData.st_blocks;
            }
            
            data->newI->insert(file.statData.st_ino);
        }
    
    return true;
}


/*
 du -s
 stat'ing a directory entry itself (not the contents) or a symlink returns a definitive
 size specific to that entry.  For reasons I don't understand the CLI 'du' command ignores
 those numbers and doesn't add them to a given subdirectory's total.  Maybe they know
 something I don't.  So this function is specifically excluding them as well in the callback.
 */
DiskStats dus(string path, set<ino_t>& seenInodes, set<ino_t>& newInodes) {
    DiskStats ds;
    dsDataType data;
    data.ds = &ds;
    data.seenI = &seenInodes;
    data.newI = &newInodes;
    
    processDirectory(path, "", false, false, dsCallback, &data);
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
    
    return select(max(rFd, wFd) + 1, rFd ? &dataSet : NULL, wFd ? &dataSet : NULL, &errorSet, &tv);
}


void showError(string message) {
    SCREENERR(message + "\n");
    log(message);
}


int copyFile(string srcFile, string destFile) {
    std::ifstream inF(srcFile, ios_base::in | ios_base::binary);
    if (!inF) return 0;
    
    std::ofstream outF(destFile, ios_base::out | ios_base::binary | ios_base::trunc);
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
    return (mylstat(name, &statBuffer) == 0);
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


// forkMvCmd is currently unused
int forkMvCmd(string oldDir, string newDir) {
    auto mv = locateBinary("/bin/mv");
    
    auto pid = fork();
    if (pid < 0) {
        SCREENERR(string("error: unable to execute fork to run mv") + errtext());
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
    
    if (!mystat(entry, &statData))
        return getFilesystemEntryType(statData.st_mode);
    
    log("error: unable to stat " + entry + errtext());
    
    return '?';
}


string readlink(string dirEntry) {
    char buf[PATH_MAX + 1];
    auto len = readlink(dirEntry.c_str(), buf, sizeof(buf) - 1);
    return ((len > 0) ? string(buf, 0, len) : "");
}


string resolveLink(string dirEntry) {
    struct stat statData;
    string prevEntry;
    
    while (!mylstat(dirEntry, &statData) && S_ISLNK(statData.st_mode)) {
        prevEntry = dirEntry;
        dirEntry = readlink(dirEntry);
        
        if (dirEntry[0] != '/')
            dirEntry = slashConcat(pathSplit(prevEntry).dir, dirEntry);
    }
    
    return dirEntry;
}


/* opendir() automatically resolves symlinks and opens the resulting directory
   when the final item in the symlink chain is a directory itself.  the original
   (symlink) name should still be used wherever possible.   all we really need is
   to know if the real name is a dir or a file, i.e. it's mode.  resolveLinkMode()
   returns the mode of the real name if it exists (is a dir, file, socket, etc)
   and the mode of the original symlink if its a dangling symlink whose antecedent
   can't be found. */
mode_t resolveLinkMode(string dirEntryName, mode_t origMode) {
    struct stat statData;
    return (mylstat(resolveLink(dirEntryName), &statData) ? origMode : statData.st_mode);
}


/*
 processDirectory() walks down the specified directory tree calling the provided
 callback() function on each filesystem entry (be it a directory, symlink or file).
 the callback needs to return true for processing to continue otherwise the rest
 of the traversal is aborted.
 
    'directory' to traverse (starting point)
 
    'pattern'
    'exclude'
    'filterDirs'
 a regex pattern can be provided to filter filenames, along with whether the pattern
 should be used as include or exclude criteria. pattern filtering can optionally be
 applied to directory names as well.

    'callback' pointer to a callback function that gets passed a pdCallbackData reference

    'passData' additional data pointer for the callback function
 
    'maxDepth' can be specified to only go x subdirectory levels deep. depth is defined as
 an entries number of subdirs below the provided 'directory'.  for example, given a provided
 directory of '/tmp/foo' and these entries:
    /tmp/foo/cases
    /tmp/foo/cases/case1
    /tmp/foo/cases/case2
 case1 and 2 are at a depth of 1 because they're one subdirectory ("cases") below the starting
 directory (/tmp/foo).  but /tmp/foo/cases itself, even though it's a directory, is at a
 depth of 0 because it's in the starting directory.
 
    'includeTopDir' whether to call the callback function for the original directory
 name that as passed in as 'directory'
 
 callback() is passed a structure that contains the full path of the filename to
 process, its stat() information, and a void pointer that can be setup before processing
 begins to pass any additional data into or out of the callback() functions.
 
 processDirectory() returns a blank string on success.  on error, the error is logged,
 shown on the screen (via SCREENERR) and returned to the calling function.
 */

string processDirectory(string directory, string pattern, bool exclude, bool filterDirs, bool (*callback)(pdCallbackData&), void *passData, int maxDepth, bool includeTopDir, bool followSymLinks) {
    DIR *dirPtr;
    size_t dirEntries;
    struct dirent *dirEntry;
    list<tuple<string, unsigned int>> dirsToRead; // filename and depth in heirarchy
    list<pdCallbackData> dirsToCallback;          // dirs to calback
    Pcre patternRE(pattern);
    struct stat dirStat;
    pdCallbackData file;
    file.dataPtr = passData;
    file.topLevelDir = directory;
    
    // dirsToRead tracks the directories that need to be scanned (opendir()/readdir())).  as new
    // subdirectories are found, they're added to the dirsToRead list so that, once we finish
    // reading the current directory, we can come back and read those subs.
    
    // dirsToCallback tracks the directories that need to have the callback function called on them.
    // the callback function is called immediately on regular files.  but for directories, it needs
    // to be called after everything else in that directory (including subdirectories) have had it
    // called.  that order enables a number of other functions, such as updating copies of files
    // before the directory they're in (so the mtime on the containing directory changes at a known
    // time) and being able to remove empty directories because the contents are processed first.
    // the result is the callback function gets called on directories in this order (depth-first):
    // /backups/2024/01/02 -> /backups/2024/01 -> /backups/2024 -> /backups
    
    // seed our list with the provided starting directory, depth of 0
    dirsToRead.push_back({ue(directory), 0});
    
    try {
        // walk through the known list of directories to read
        while (!dirsToRead.empty()) {
            auto [baseDir, depth] = dirsToRead.front();
            dirsToRead.pop_front();
            dirEntries = 0;
            
            // verify we have an accessible directory
            if (!mylstat(baseDir, &dirStat)) {
                                
                if (S_ISDIR(dirStat.st_mode) || (followSymLinks && S_ISDIR(resolveLinkMode(baseDir, dirStat.st_mode)))) {
                    
                    // read individual directory entries
                    if ((dirPtr = opendir(baseDir.c_str())) != NULL) {
                        while ((dirEntry = readdir(dirPtr)) != NULL) {
                            
                            if (!strcmp(dirEntry->d_name, ".") || !strcmp(dirEntry->d_name, ".."))
                                continue;
                            
                            ++dirEntries;
                            file.filename = slashConcat(baseDir, dirEntry->d_name);
                            
                            if (!mylstat(file.filename, &file.statData)) {
                                
                                /* process directories - first we filter (if requested) to make sure we're
                                 interested in this subdirectory.  if so, and its within our depth constraints,
                                 we add it to the list of directories to read. */
                                if (S_ISDIR(file.statData.st_mode) || (followSymLinks && S_ISDIR(resolveLinkMode(file.filename, file.statData.st_mode)))) {
                                    
                                    // filter for patterns
                                    if (filterDirs)
                                        if (pattern.length()) {
                                            bool found = patternRE.search(file.filename);
                                            
                                            if ((exclude && found) || (!exclude && !found))
                                                continue;
                                        }
                                    
                                    if (maxDepth < 1 || depth < maxDepth)
                                        dirsToRead.push_back({file.filename, depth+1});
                                }
                                else {
                                    /* process files - again, first filter (if requested) to make sure we're
                                     interested in this file.  if so, immediately call the callback on it. */
                                    if (pattern.length()) {
                                        bool found = patternRE.search(file.filename);
                                        
                                        if ((exclude && found) || (!exclude && !found))
                                            continue;
                                    }
                                    
                                    file.dirEntries = 0;
                                    if (!callback(file)) {
                                        dirsToRead.clear();
                                        break;
                                    }
                                }
                            }
                        }
                        closedir(dirPtr);
                        
                        /* here we've finished reading everything in the current directory.  we can't call the
                         callback on the directory itself because we may have found subdirectories that need
                         to be processed first.  those subs will get handled as we get back to the top of our
                         while().  so we add the current directory to the dirsToCallback list that will get
                         processed after all the directory walking is complete. */
                        if (includeTopDir || baseDir != directory) {
                            file.filename = baseDir;
                            file.statData = dirStat;
                            file.dirEntries = dirEntries;
                            file.depth = depth - 1;
                            dirsToCallback.push_front(file);
                        }
                    }
                    else {
                        string err = "error: unable to open " + ue(directory) + errtext();;
                        SCREENERR(log(err));
                        return err;
                    }
                }
                else {
                    // in case we're given an initial file instead of directory
                    file.filename = baseDir;
                    file.statData = dirStat;
                    file.depth = depth;
                    callback(file);
                }
            }
            else {
                string err = "error: stat failed for " + baseDir + errtext();
                // SCREENERR(log(err));
                return err;
            }
        }
        
        /* all the directory walking is complete.  now we can call the callback on each directory we've found
         in the proper depth-first order. */
        while (!dirsToCallback.empty()) {
            file = dirsToCallback.front();
            dirsToCallback.pop_front();
            
            if (!callback(file))
                break;
        }
    }
    catch (MBException &e) {
        SCREENERR("error: " << e.detail());
        log("error: " + e.detail());
        return e.detail();
    }
    
    return "";
}


struct internalPDBDataType {
    bool (*realCallback)(pdCallbackData&);
    void *realDataPtr;
    backupTypes backupType;
    Pcre *backupPattern;
};


bool pdBackupsCallback(pdCallbackData &file) {
    internalPDBDataType *data = (internalPDBDataType*)file.dataPtr;
    pdCallbackData passedFile;
    passedFile = file;
    passedFile.dataPtr = data->realDataPtr;
        
    // make sure we're in the year/month or year/month/day subdirs
    if (data->backupPattern->search(file.filename)) {
        
        // handle directories
        if (S_ISDIR(file.statData.st_mode)) {
            if (data->backupType != SINGLE_ONLY)
                return data->realCallback(passedFile);
        }
        
        // handle files
        else
            if (data->backupType != FAUB_ONLY)
                return data->realCallback(passedFile);
    }
    
    return true;
}


/*
 processDirectoryBackups() is a version of processDirectory that only returns backups.
 for single-file backups the file is returned, for faub backups the containing directory
 is returned.  backupType specifies which types to return.
 */
string processDirectoryBackups(string directory, string pattern, bool filterDirs, bool (*callback)(pdCallbackData&), void *passData, backupTypes backupType, int maxDepth, bool followSymLinks) {
    internalPDBDataType data;
    Pcre backupPattern("./\\d{4}/\\d{2}(?:/\\d{2}){0,1}/(?!\\d{2}\\b)[^/]+$");  // identify backup directories
    data.backupPattern = &backupPattern;
    data.realCallback = callback;
    data.realDataPtr = passData;
    data.backupType = backupType;
        
    return processDirectory(directory, "(/\\d{2,4}$)|(" + pattern + ")", false, filterDirs, pdBackupsCallback, &data, maxDepth == -1 ? 4 : maxDepth, false, followSymLinks);
}


string progressPercentageA(int totalIterations, int totalSteps, int iterationsComplete, int stepsComplete, string detail) {
    static int prevLength = 0;
    string result;
    
    // start with backspaces to erase our previous message
    if (totalIterations < 0)
        prevLength = 0;
    else
        if (prevLength)
            result = string(prevLength, '\b') + string(prevLength, ' ') + string(prevLength, '\b');
    
    if (totalIterations > 0) {
        int target = totalIterations * totalSteps;
        int current = iterationsComplete * totalSteps + stepsComplete;
        string currentProgress = to_string(int((float)current / (float)target * 100)) + "% " + detail + " ";
        prevLength = (int)currentProgress.length();
        result += currentProgress;
    }
    
    return result;
}

string progressPercentageB(long totalBytes, long completedBytes) {
    static int prevLength = 0;
    string result;
    
    if (prevLength)
        result = string(prevLength, '\b') + string(prevLength, ' ') + string(prevLength, '\b');
    
    if (totalBytes > 0) {
        string currentProgress = to_string(long((long double)completedBytes / (long double)totalBytes * 100)) + "%";
        prevLength = (int)currentProgress.length();
        result += currentProgress;
    }
    else
        prevLength = 0;
    
    return result;
}


int mylstat(string filename, struct stat *buf) {
    ++GLOBALS.statsCount;
    return (lstat(filename.c_str(), buf));
}


int mystat(string filename, struct stat *buf) {
    ++GLOBALS.statsCount;
    return (stat(filename.c_str(), buf));
}


string errtext(bool format) {
    return((format ? " - " : "") + string(strerror(errno)));
}


// replace carriage-returns with commas
string commafy(string data) {
    if (data.back() == '\n')
        data.pop_back();
    
    size_t pos = 0;
    while((pos = data.find("\n", pos)) != std::string::npos) {
        data.replace(pos, 1, ", ");
        pos += 2;
    }
    return data;
}


string resolveGivenDirectory(string inputDir, bool allowWildcards) {
    char dirBuf[PATH_MAX+1];

    if (!allowWildcards && (inputDir.find("*") != string::npos || inputDir.find("?") != string::npos)) {
        SCREENERR("error: specific directory required (no wildcards): '" << inputDir << "'");
        exit(1);
    }
    
    // prepend pwd
    if (inputDir[0] != '/' && inputDir[0] != '~') {
        if (getcwd(dirBuf, sizeof(dirBuf)) == NULL) {
            SCREENERR("error: unable to determine the current directory");
            exit(1);
        }
        
        inputDir = slashConcat(dirBuf, inputDir);
    }
    
    // handle tilde substitution
    if (inputDir[0] == '~') {
        string user;
        auto slash = inputDir.find("/");
        
        if (slash != string::npos)
            user = inputDir.substr(1, slash - 1);
        else
            user = inputDir.length() ? inputDir.substr(1, inputDir.length() - 1) : "";
        
        auto uid = getUidFromName(user);
        if (uid < 0) {
            SCREENERR("error: invalid user reference in ~" << user);
            exit(1);
        }
        
        auto homeDir = getUserHomeDir(uid);
        if (!homeDir.length()) {
            SCREENERR("error: unable to determine home directory for ~" + user);
            exit(1);
        }
        
        inputDir.replace(0, slash, homeDir);
    }
    
    return inputDir;
}


bool isSameFileSystem(string dirEntry1, string dirEntry2) {
    struct stat statData1;
    struct stat statData2;
    
    if (mystat(dirEntry1, &statData1)) {
        SCREENERR("error: unable to access (stat) " << dirEntry1 << errtext());
        exit(1);
    }
    
    if (mystat(dirEntry2, &statData2)) {
        SCREENERR("error: unable to access (stat) " << dirEntry2 << errtext());
        exit(1);
    }
    
    return (statData1.st_dev == statData2.st_dev);
}


// realpath() wouldn't tell us if two absolute paths referred to the
// same file via hard links.  so let's use a manual approach to be certain.
bool isSameDirectory(string dirEntry1, string dirEntry2) {
    string testFilename = ".linktest." + to_string(getpid());
    string test1 = slashConcat(dirEntry1, testFilename);
    string test2 = slashConcat(dirEntry2, testFilename);
    bool result = false;
    
    ofstream ofile;
    ofile.open(test1);
    if (ofile.is_open()) {
        ofile.close();
        
        result = exists(test2);
        unlink(test1.c_str());
    }
    
    return result;
}



bool statusMessage::show(string newMessage) {
    auto ll = lastMessage.length();
    auto nl = newMessage.length();
    
    cout << (shown ? string(lastMessage.length(), '\b') : "") << (newMessage.length() ? newMessage : lastMessage);
    
    if (shown && nl && ll && nl < ll)
        cout << string(ll - nl, ' ') << string(ll - nl, '\b');
    
    cout << flush;
    
    if (nl)
        lastMessage = newMessage;
    
    shown = true;
    return shown;
}

bool statusMessage::remove() {
    if (shown) {
        string bs = string(lastMessage.length(), '\b');
        cout << bs << string(lastMessage.length(), ' ') << bs << flush;
    }
    
    shown = false;
    return shown;
}


bool statModeOwnerTimeEqual(struct stat a, struct stat b) {
    return (a.st_uid == b.st_uid &&
            a.st_gid == b.st_gid &&
#ifdef __APPLE__
            a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec &&
            a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
#else
            a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
            a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
#endif
            a.st_mode == b.st_mode);
}
