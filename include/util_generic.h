
#ifndef UTIL_GENERIC
#define UTIL_GENERIC

#include <string>
#include <time.h>
#include <sys/time.h>
#include <set>
#include <iostream>

#include "globals.h"
#include "math.h"
#include "globals.h"


using namespace std;

struct DiskStats {
    size_t sizeInBytes;
    size_t sizeInBlocks;
    size_t savedInBytes;
    size_t savedInBlocks;

    DiskStats(size_t szBy = 0, size_t szBk = 0, size_t svBy = 0, size_t svBk = 0) {
        sizeInBytes = szBy;
        sizeInBlocks = szBk;
        savedInBytes = svBy;
        savedInBlocks = svBk;
    }

    DiskStats& operator+=(const DiskStats& a) {
        sizeInBytes += a.sizeInBytes;
        sizeInBlocks += a.sizeInBlocks;
        savedInBytes += a.savedInBytes;
        savedInBlocks += a.savedInBlocks;
        return *this;
    }

    size_t getSize() { return (GLOBALS.useBlocks ? sizeInBlocks : sizeInBytes); }
    size_t getSaved() { return (GLOBALS.useBlocks ? savedInBlocks : savedInBytes); }
    
    friend ostream& operator<<(ostream& s, const DiskStats& a) {
        s << "usedBy:" << a.sizeInBytes << ", usedBk:" << a.sizeInBlocks << ", savBy:" << a.savedInBytes << ", savBk:" << a.savedInBlocks;
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


string cppgetenv(string variable);

string plural(size_t number, string text);

string plurali(size_t number, string text);

string log(string message);

struct timeval mktimeval(time_t secs);

string timeDiff(struct timeval start, struct timeval end = mktimeval(GLOBALS.startupTime), int maxUnits = 2, int precision = 2);

vector<string> perlSplit(string regex, string haystack);

class timer {
    string duration;

    public:
        struct timeval startTime;
        struct timeval endTime;
    
        void start() { gettimeofday(&startTime, NULL); duration = ""; }
        void stop() { gettimeofday(&endTime, NULL); }

        time_t seconds() { 
            unsigned long totalus = (endTime.tv_sec * MILLION + endTime.tv_usec) - (startTime.tv_sec * MILLION + startTime.tv_usec);
            return floor(1.0 * totalus / MILLION);

            auto r = endTime.tv_sec - startTime.tv_sec - (endTime.tv_usec > startTime.tv_usec ? 1 : 0); 
            return(r > 0 ? r : 0); 
        }

        string elapsed(int precision = 2) {
            if (!duration.length()) 
                duration = timeDiff(startTime, endTime, 3, precision);

            return(duration);
        }

        timer() { startTime.tv_sec = 0; startTime.tv_usec = 0; }
};


struct s_pathSplit {
    string dir;
    string file;
};


s_pathSplit pathSplit(string path);

string slashConcat(string str1, string str2);
string slashConcat(string str1, string str2, string str3);

string MD5file(string filename, bool quiet = 0);
string MD5string(string data);

string onevarsprintf(string format, string data);

string approximate(size_t size, int maxUnits = -1, bool commas = false);

string seconds2hms(time_t seconds);

string dw(int which);

int mkdirp(string dir);

string trimSpace(const string &s);

string trimQuotes(string s);

string safeFilename(string filename);

int varexec(string fullCommand);

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

bool rmrf(string dir);

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

#endif

