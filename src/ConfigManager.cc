
#include <fstream>
#include <dirent.h>
#include "pcre++.h"

#include "ConfigManager.h"
#include "globals.h"
#include "util.h"

using namespace pcrepp;


int ConfigManager::config(string title) {
    int index = 0;
    for (auto cfg_it = configs.begin(); cfg_it != configs.end(); ++cfg_it) {
        ++index;
        if ((*cfg_it).settings[sTitle].value == title)   
            return index;
    }

    return 0;
}


ConfigManager::ConfigManager() {
    DIR *c_dir;
    struct dirent *c_dirEntry;

    if ((c_dir = opendir(CONF_DIR)) != NULL ) {
        Pcre regEx(".*\\.conf$");

        // loop through *.conf files
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, "..") || 
                    !strcmp(c_dirEntry->d_name, "managebackups.conf") || !regEx.search(string(c_dirEntry->d_name)))
                continue;

            string fullFilename = addSlash(string(CONF_DIR)) + string(c_dirEntry->d_name);
            BackupConfig backupConfig;
            backupConfig.loadConfig(fullFilename);
            configs.insert(configs.begin(), backupConfig);
        }

        closedir(c_dir);
    }

    activeConfig = -1;
}


void ConfigManager::fullDump() {
    for (auto c_it = configs.begin(); c_it != configs.end(); ++c_it) {
        c_it->fullDump();
    }
}


void ConfigManager::loadAllConfigCaches() {
    for (auto c_it = configs.begin(); c_it != configs.end(); ++c_it) {
        c_it->loadConfigsCache();
    }
}


