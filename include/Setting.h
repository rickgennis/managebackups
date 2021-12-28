
#ifndef SETTING_H
#define SETTING_H

#include <variant>
#include <string>
#include <pcre++.h>

using namespace std;
using namespace pcrepp;


#define SETTINGVARIANT variant<int, string, vector<string> >

enum SetType { INT, STRING };
enum SetSpecifier { sTitle, sDirectory, sBackupFilename, sBackupCommand, sDays, sWeeks, sMonths, sYears, sFailsafeBackups, sFailsafeDays,
    sCPTo, sSFTPTo, sNotify };

class Setting {
    public:
        string display_name;
        enum SetType data_type;
        string value;
        string defaultValue;
        Pcre regex;
        bool seen;

        //string getString(bool getDefault = 0);
        //void setValue(string newValue);
        //void setValue(int newValue);
        Setting(string name, string pattern, enum SetType setType, string defaultVal);
};

#endif

