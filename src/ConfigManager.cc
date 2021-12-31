
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
    int index = -1;
    for (auto c_it = configs.begin(); c_it != configs.end(); ++c_it) {
        ++index;
        cout << endl;
        cout << "config " << index << " (days): " << c_it->settings[sDays].value << endl;
        cout << "config " << index << " (months): " << c_it->settings[sMonths].value << endl;
        cout << "config " << index << " (title): " << c_it->settings[sTitle].value << endl;
        cout << "config " << index << " (dir): " << c_it->settings[sDirectory].value << endl;
        cout << "config " << index << " (file): " << c_it->settings[sBackupFilename].value << endl;
        cout << "config " << index << " (cp): " << c_it->settings[sCPTo].value << endl;
        cout << "config " << index << " (config_filename): " << c_it->config_filename << endl;
    }
}


void ConfigManager::loadAllConfigCaches() {
    for (auto c_it = configs.begin(); c_it != configs.end(); ++c_it) {
        c_it->loadConfigsCache();
    }
}


