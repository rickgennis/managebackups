
#ifndef UTIL_GENERIC
#define UTIL_GENERIC

#include <string>
#include <time.h>
#include <sys/time.h>
#include <set>
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

string s(int number);

string ies(int number);

void log(string message);

struct timeval mktimeval(unsigned long secs);

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

string approximate(double size, int maxUnits = -1, bool commas = false);

string seconds2hms(unsigned long seconds);

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

unsigned long approx2bytes(string approx);

string todayString();

string blockp(string data, int width);

string catdir(string dir);

bool rmrfdir(string dir);

int mkbasedirs(string path);

// du -s
DiskStats dus(string path, set<ino_t>& seenInodes, set<ino_t>& newInodes);
DiskStats dus(string path);

string errorcom(string profile, string message);

#endif

