
#include <fstream>
#include <dirent.h>
#include "pcre++.h"

#include "ConfigManager.h"
#include "globals.h"
#include "util.h"

using namespace pcrepp;


const BackupConfig *ConfigManager::config(string title) {
    for (auto cfg_it = configs.begin(); cfg_it != configs.end(); ++cfg_it)
        if ((*cfg_it).title == title) {
            return &(*cfg_it);
        }

    return NULL;
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
}



