
#ifndef SETTING_H
#define SETTING_H

#include <string>
#include <pcre++.h>

using namespace std;
using namespace pcrepp;

enum SetType { INT, STRING, BOOL };
enum SetSpecifier { sTitle, sDirectory, sBackupFilename, sBackupCommand, sDays, sWeeks, sMonths, sYears, sFailsafeBackups, sFailsafeDays,
    sSCPTo, sSFTPTo, sPruneLive, sNotify, sMaxLinks, sIncTime, sNos };

class Setting {
    public:
        string display_name;
        enum SetType data_type;
        string value;
        string defaultValue;
        Pcre regex;
        bool seen;

        int ivalue() { return stoi(value); }
        Setting(string name, string pattern, enum SetType setType, string defaultVal);
};

#endif

