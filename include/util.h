
#ifndef UTIL_GENERIC
#define UTIL_GENERIC

#include <string>

using namespace std;


void log(string message);

string addSlash(string str);

string MD5file(string filename);
string MD5string(string data);

#endif

