
#ifndef UTIL_GENERIC
#define UTIL_GENERIC

#include <string>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <set>
#include <iostream>

#include "pcre++.h"
#include "globals.h"
#include "math.h"
#include "globals.h"

using namespace pcrepp;
using namespace std;


struct DiskStats {
    size_t usedInBytes;
    size_t usedInBlocks;
    size_t savedInBytes;
    size_t savedInBlocks;
    
    // these three are just temp storage
    size_t dirs;
    size_t symLinks;
    size_t mods;
    
    DiskStats(size_t szBy = 0, size_t szBk = 0, size_t svBy = 0, size_t svBk = 0, size_t sDirs = 0, size_t sSymLinks = 0, size_t sMods = 0) {
        usedInBytes = szBy;
        usedInBlocks = szBk;
        savedInBytes = svBy;
        savedInBlocks = svBk;
        
        dirs = sDirs;
        symLinks = sSymLinks;
        mods = sMods;
    }

    DiskStats& operator+=(const DiskStats& a) {
        usedInBytes += a.usedInBytes;
        usedInBlocks += a.usedInBlocks;
        savedInBytes += a.savedInBytes;
        savedInBlocks += a.savedInBlocks;
        dirs += a.dirs;
        symLinks += a.symLinks;
        
        return *this;
    }

    size_t getSize() { return (GLOBALS.useBlocks ? usedInBlocks : usedInBytes); }
    size_t getSaved() { return (GLOBALS.useBlocks ? savedInBlocks : savedInBytes); }
    
    friend ostream& operator<<(ostream& s, const DiskStats& a) {
        s << "usedBy:" << a.usedInBytes << ", usedBk:" << a.usedInBlocks << ", savBy:" << a.savedInBytes << ", savBk:" << a.savedInBlocks;
        return s;
    }
};


#define ifcolor(x) (GLOBALS.color ? x : "")

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })


#define mytimersub(tvp, uvp, vvp)                         \
    do {                                                  \
        (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;    \
        (vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec; \
        if ((vvp)->tv_usec < 0) {                         \
            (vvp)->tv_sec--;                              \
            (vvp)->tv_usec += 1000000;                    \
        }                                                 \
    } while (0)

#define mytimeradd(tvp, uvp, vvp)                         \
    do {                                                  \
        (vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;    \
        (vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec; \
        if ((vvp)->tv_usec >= 1000000) {                  \
            (vvp)->tv_sec++;                              \
            (vvp)->tv_usec -= 1000000;                    \
        }                                                 \
    } while (0)


string cppgetenv(string variable);

string plural(size_t number, string text);

string plurali(size_t number, string text);

string log(string message);

struct timeval mktimeval(time_t secs);

string timeDiff(struct timeval start, struct timeval end = mktimeval(GLOBALS.startupTime), int maxUnits = 2, int precision = 2);
string timeDiffSingle(struct timeval, int maxUnits = 2, int precision = 2);

string perlJoin(string delimiter, vector<string> items);

vector<string> perlSplit(string regex, string haystack);

void splitOnRegex(vector<string>& result, string data, Pcre& re, bool trimQ, bool unEscape);


class timer {
    string duration;
    struct timeval startTime;
    struct timeval endTime;
    struct timeval spentTime;

    public:    
        void start() { gettimeofday(&startTime, NULL); duration = ""; }
        void stop() {
            gettimeofday(&endTime, NULL);
            struct timeval diffTime;
            struct timeval tempTime;
            mytimersub(&endTime, &startTime, &diffTime);
            mytimeradd(&diffTime, &spentTime, &tempTime);
            spentTime = tempTime;
        }
    
        void restart() { spentTime.tv_sec = spentTime.tv_usec = 0; start(); }
    
        time_t seconds() { return (spentTime.tv_sec); }
        long int useconds() { return (spentTime.tv_usec); }

        string elapsed(int precision = 2) {
            if (!duration.length()) 
                duration = timeDiffSingle(spentTime, 3, precision);

            return(duration);
        }

    time_t getEndTimeSecs() { return endTime.tv_sec; }
        
    timer() { startTime.tv_sec = startTime.tv_usec = endTime.tv_sec = endTime.tv_usec = spentTime.tv_sec = spentTime.tv_usec = 0; }
};


struct s_pathSplit {
    string dir;
    string file;
    string file_base;
    string file_ext;
};

// pathsplit assumes a full dir/file
s_pathSplit pathSplit(string path);

// getDirUp assumes just a dir path, no file on the end.  so
// we can use pathSplit to chop off the last portion, which pathSplit
// will think is the file but we know to be the last dir here.
string getDirUp(string path);

string slashConcat(string str1, string str2, string str3 = "");

string MD5file(string filename, bool quiet = 0, string reason = "");
string MD5string(string data);

string onevarsprintf(string format, string data);

string approximate(size_t size, int maxUnits = -1, bool commas = false, bool base10 = false);

string seconds2hms(time_t seconds);

string dw(int which);

void setFilePerms(string filename, struct stat &statData, bool exitOnError = true);

int mkdirp(string dir, mode_t mode = 0775);
void mkdirp(string dir, struct stat &statData);

string trimSpace(const string &s);

string trimQuotes(string s, bool unEscape = false);

string safeFilename(string filename);

int varexec(string fullCommand);

vector<string> string2vectorOnPipe(string data, bool trimQ = false, bool unEscape = false);
vector<string> string2vectorOnSpace(string data, bool trimQ = false, bool unEscape = false);

vector<string> expandWildcardFilespec(string filespec);

void strReplaceAll(string& s, string const& toReplace, string const& replaceWith);

string locateBinary(string app);

bool str2bool(string text);

string vars2MY(int month, int year);

bool mtimesAreSameDay(time_t m1, time_t m2);

string horizontalLine(int length);

void sendEmail(string from, string recipients, string subject, string message);

size_t approx2bytes(string approx);

string todayString();

string blockp(string data, int width);

string catdir(string dir);

bool rmrf(string directory, bool includeTopDir = true);

int mkbasedirs(string path);

// du -s
DiskStats dus(string path, set<ino_t>& seenInodes, set<ino_t>& newInodes);
DiskStats dus(string path);

string errorcom(string profile, string message);

int simpleSelect(int rFd, int wFd, int timeoutSecs);

void showError(string message);

int copyFile(string srcFile, string destFile);

// unescape
string ue(string file);

bool exists(const std::string& name);

string getUserHomeDir(int uid = -1);

int getUidFromName(string userName);

time_t filename2Mtime(string filename);

int forkMvCmd(string oldDir, string newDir);

string realpathcpp(string origPath);

char getFilesystemEntryType(mode_t mode);
char getFilesystemEntryType(string entry);

struct pdCallbackData {
    string filename;
    unsigned int depth;
    size_t dirEntries;
    string topLevelDir;
    struct stat statData;
    void *dataPtr;
};

enum backupTypes { SINGLE_ONLY, FAUB_ONLY, ALL_BACKUPS };

string processDirectory(string directory, string pattern, bool exclude, bool filterDirs, bool (*callback)(pdCallbackData&), void *passData, int maxDepth = -1, bool includeTopDir = false, bool followSymLinks = false);
string processDirectoryBackups(string directory, string pattern, bool exclude, bool (*callback)(pdCallbackData&), void *passData, backupTypes backupType, int maxDepth = -1, bool followSymLinks = true);

string progressPercentageA(int totalIterations, int totalSteps = 7, int iterationsComplete = 0, int stepsComplete = 0, string detail = "");
string progressPercentageB(long totalBytes, long completedBytes);

int mylstat(string filename, struct stat *buf);
int mystat(string filename, struct stat *buf);

string errtext(bool format = true);

unsigned int ansistrlength(string source);

string commafy(string data);

string readlink(string dirEntry);

mode_t resolveLinkMode(string dirEntryName, mode_t origMode);

string resolveGivenDirectory(string inputDir, bool allowWildcards = true);

bool isSameFileSystem(string dirEntry1, string dirEntry2);

bool isSameDirectory(string dirEntry1, string dirEntry2);

class statusMessage {
    string lastMessage;
    bool shown;
public:
    statusMessage(string text = "") { lastMessage = text; shown = false; }
    bool show(string text = "");
    bool remove();
};

bool statModeOwnerTimeEqual(struct stat a, struct stat b);

vector<string> fullRegexMatch(string re, string data);


class firstTimeOnly {
    bool firstDone;
public:
    firstTimeOnly() : firstDone(false) {};
    bool firstRun() {
        auto state = firstDone;
        firstDone = true;
        return !state;
    }
};

#endif

