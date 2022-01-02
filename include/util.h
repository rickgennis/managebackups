
#ifndef UTIL_GENERIC
#define UTIL_GENERIC

#include <string>
#include "globals.h"

using namespace std;

#define ifcolor(x) (GLOBALS.color ? x : "")

string s(int number);

void log(string message);

string addSlash(string str);

string MD5file(string filename);
string MD5string(string data);

string onevarsprintf(string format, string data);

string approximate(double size);

string seconds2hms(unsigned long seconds);

string timeDiff(unsigned long start, unsigned long end = GLOBALS.startupTime, int maxUnits = 2);

string dw(int which);

#endif

