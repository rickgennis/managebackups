
#ifndef UTIL_GENERIC
#define UTIL_GENERIC

#include <string>
#include <time.h>
#include <sys/time.h>
#include "globals.h"

using namespace std;

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

void log(string message);

struct timeval mktimeval(unsigned long secs);

string timeDiff(struct timeval start, struct timeval end = mktimeval(GLOBALS.startupTime), int maxUnits = 2, int precision = 2);


class timer {
    string duration;

    public:
        struct timeval startTime;
        struct timeval endTime;
    
        void start() { gettimeofday(&startTime, NULL); duration = ""; }
        void stop() { gettimeofday(&endTime, NULL); }

        time_t seconds() { 
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

string addSlash(string str);

string MD5file(string filename, bool quiet = 0);
string MD5string(string data);

string onevarsprintf(string format, string data);

string approximate(double size);

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

void sendEmail(string recipients, string subject, string message);

unsigned long approx2bytes(string approx);

string todayString();

string blockp(string data, int width);

#endif

