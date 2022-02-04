
#ifndef SETTING_H
#define SETTING_H

#include <string>
#include <pcre++.h>
#include <util_generic.h>

using namespace std;
using namespace pcrepp;

enum SetType { INT, STRING, BOOL, OCTAL, SIZE };
enum SetSpecifier { sTitle, sDirectory, sBackupFilename, sBackupCommand, sDays, sWeeks, sMonths, sYears, sFailsafeBackups, sFailsafeDays,
    sSCPTo, sSFTPTo, sPruneLive, sNotify, sMaxLinks, sIncTime, sNos, sMinSize, sDOW, sFP, sMode, sMinSpace, sMinSFTPSpace };

class Setting {
    public:
        string display_name;
        enum SetType data_type;
        string value;
        string defaultValue;
        Pcre regex;
        bool seen;

        int ivalue() { return stoi(value); }
        string confPrint(string sample = "");
        Setting(string name, string pattern, enum SetType setType, string defaultVal);
};

#endif

