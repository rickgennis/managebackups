
#include <fstream>
#include <dirent.h>
#include "pcre++.h"
#include <algorithm>
#include "ConfigManager.h"
#include "globals.h"
#include "util_generic.h"

using namespace pcrepp;


int ConfigManager::findConfig(string title) {
    int index = 0;
    int partialMatchIdx = 0;

    for (auto &config: configs) {
        ++index;

        if (config.settings[sTitle].value == title)   
            return index;

        if (config.settings[sTitle].value.find(title) != string::npos) {
            if (partialMatchIdx)
                return -1;
            else
                partialMatchIdx = index;
        }
    }

    if (partialMatchIdx)
        return partialMatchIdx;

    return 0;
}


bool configMgrCallback(pdCallbackData &file) {
    if (!S_ISDIR(file.statData.st_mode)) {
        vector<BackupConfig> *configs = (vector<BackupConfig>*)file.dataPtr;
        BackupConfig backupConfig;
        backupConfig.loadConfig(file.filename);
        
        auto foundConfig = find(configs->begin(), configs->end(), backupConfig);
        if (foundConfig != configs->end()) {
            SCREENERR("error: duplicate profile name (" << foundConfig->settings[sTitle].value << ") defined in " << file.filename << " and " << foundConfig->config_filename);
            exit(1);
        }
            
        configs->emplace(configs->begin(), backupConfig);
    }
    
    return true;
}

    
ConfigManager::ConfigManager() {
    processDirectory(ue(GLOBALS.confDir), "\\.conf$", false, configMgrCallback, &configs);
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


bool housekeepingCallback(pdCallbackData &file) {
    vector<BackupConfig> *configs = (vector<BackupConfig>*)file.dataPtr;

    for (auto &config: *configs) {
        if (config.settings[sUUID].value == pathSplit(file.filename).file)
            return true;
    }
    
    DEBUG(D_cache) DFMT("removing orphaned cache file " << file.filename);
    rmrf(file.filename);
    
    return true;
}

// scan cache sub-dirs and remove any that are no longer associated
// with an active profile;  i.e. clean up the cache uuid sub-dirs
void ConfigManager::housekeeping() {
    processDirectory(GLOBALS.cacheDir, "/\\w{32}$", false, housekeepingCallback, &configs, 1);
}
