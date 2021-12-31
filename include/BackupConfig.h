
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
    public:
        string config_filename;

        bool modified;
        bool temp;
        vector<Setting> settings;
        BackupCache cache;

        BackupConfig(bool makeTemp = 0);
        ~BackupConfig();

        void fullDump();

        void saveConfig();
        bool loadConfig(string filename);
        void loadConfigsCache();
};

#endif

