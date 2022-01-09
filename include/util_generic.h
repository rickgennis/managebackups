
#ifndef UTIL_GENERIC
#define UTIL_GENERIC

#include <string>
#include "globals.h"

using namespace std;

#define ifcolor(x) (GLOBALS.color ? x : "")

string s(int number);

void log(string message);

struct s_pathSplit {
    string dir;
    string file;
};

s_pathSplit pathSplit(string path);

string addSlash(string str);

string MD5file(string filename);
string MD5string(string data);

string onevarsprintf(string format, string data);

string approximate(double size);

string seconds2hms(unsigned long seconds);

string timeDiff(unsigned long start, unsigned long end = GLOBALS.startupTime, int maxUnits = 2);

string dw(int which);

void mkdirp(string dir);

string trimSpace(const string &s);

string trimQuotes(string s);

int varexec(string fullCommand);

#endif

