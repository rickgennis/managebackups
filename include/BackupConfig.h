
#ifndef BACKUPCONFIG_H
#define BACKUPCONFIG_H

#include <vector>
#include <variant>
#include <string>
#include <pcre++.h>
#include "Setting.h"
#include "BackupCache.h"

using namespace std;
using namespace pcrepp;


class BackupConfig {
        string config_filename;

    public:
        bool modified;
        vector<Setting> settings;
        BackupCache cache;

        BackupConfig(bool temp = 0);
        ~BackupConfig();

        void saveConfig();
        bool loadConfig(string filename);
};

#endif

