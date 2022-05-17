
#include <fstream>
#include <dirent.h>
#include "pcre++.h"
#include <algorithm>
#include "ConfigManager.h"
#include "globals.h"
#include "util_generic.h"

using namespace pcrepp;


int ConfigManager::config(string title) {
    int index = 0;
    for (auto &config: configs) {
        ++index;
        if (config.settings[sTitle].value == title)   
            return index;
    }

    return 0;
}


ConfigManager::ConfigManager() {
    DIR *c_dir;
    struct dirent *c_dirEntry;

    if ((c_dir = opendir(GLOBALS.confDir.c_str())) != NULL ) {
        Pcre regEx(".*\\.conf$");

        // loop through *.conf files
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, "..") || 
                    !strcmp(c_dirEntry->d_name, "managebackups.conf") || !regEx.search(string(c_dirEntry->d_name)))
                continue;

            string fullFilename = slashConcat(GLOBALS.confDir, c_dirEntry->d_name);
            BackupConfig backupConfig;
            backupConfig.loadConfig(fullFilename);
            configs.insert(configs.begin(), backupConfig);
        }

        closedir(c_dir);
    }

    activeConfig = -1;
    sort(configs.begin(), configs.end());
}


void ConfigManager::fullDump() {
    for (auto &config: configs)
        config.fullDump();
}


void ConfigManager::loadAllConfigCaches() {
    for (auto &config: configs)
        config.loadConfigsCache();
}


