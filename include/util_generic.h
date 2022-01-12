
#ifndef UTIL_GENERIC
#define UTIL_GENERIC

#include <string>
#include <time.h>
#include "globals.h"

using namespace std;

#define ifcolor(x) (GLOBALS.color ? x : "")

string s(int number);

void log(string message);

string timeDiff(unsigned long start, unsigned long end = GLOBALS.startupTime, int maxUnits = 2);


class timer {
    string duration;

    public:
        time_t startTime;
        time_t endTime;
    
        void start() { time(&startTime); duration = ""; }
        void stop() { time(&endTime); }

        time_t seconds() { return(endTime - startTime); }

        string elapsed() {
            if (!duration.length()) 
                duration = timeDiff(startTime, endTime, 3);

            return(duration);
        }

        timer() { startTime = 0; }
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

void mkdirp(string dir);

string trimSpace(const string &s);

string trimQuotes(string s);

string safeFilename(string filename);

int varexec(string fullCommand);

vector<string> expandWildcardFilespec(string filespec);

void strReplaceAll(string& s, string const& toReplace, string const& replaceWith);

string locateBinary(string app);

bool str2bool(string text);

#endif

