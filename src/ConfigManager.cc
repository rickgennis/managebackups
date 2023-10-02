
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
        ConfigManager *configManager = (ConfigManager*)file.dataPtr;
        BackupConfig backupConfig;
        backupConfig.loadConfig(file.filename);
        
        auto foundConfig = find(configManager->configs.begin(), configManager->configs.end(), backupConfig);
        
        if (foundConfig != configManager->configs.end()) {
            SCREENERR("error: duplicate profile name (" << foundConfig->settings[sTitle].value << ") defined in " << file.filename << " and " << foundConfig->config_filename);
            exit(1);
        }
        
        if (str2bool(backupConfig.settings[sDefault].value)){
            if (configManager->defaultConfig.length()) {
                SCREENERR("error: only one profile can be set as the default");
                exit(1);
            }
            
            configManager->defaultConfig = backupConfig.settings[sTitle].value;
        }
        
        if (backupConfig.settings[sPaths].value.length() &&
            (backupConfig.settings[sBackupCommand].value.length() || backupConfig.settings[sBackupFilename].value.length())) {
            SCREENERR("error: 'paths' is mutually-exclusive with 'command' and 'file' in profile " << backupConfig.settings[sTitle].value);
            exit(1);
        }
        
        if (backupConfig.settings[sFaub].value.length() &&
            (backupConfig.settings[sBackupCommand].value.length() || backupConfig.settings[sBackupFilename].value.length())) {
            SCREENERR("error: 'faub' is mutually-exclusive with 'command' and 'file' in profile " << backupConfig.settings[sTitle].value);
            exit(1);
        }
        
        if (backupConfig.settings[sPaths].value.length() &&
            backupConfig.settings[sFaub].value.length()) {
            SCREENERR("error: 'paths' and 'faub' are mutually-exclusive in profile " << backupConfig.settings[sTitle].value);
            exit(1);
        }
   
        configManager->configs.emplace(configManager->configs.begin(), backupConfig);
    }
    
    return true;
}

    
ConfigManager::ConfigManager() {
    activeConfig = -1;
    defaultConfig = "";
    processDirectory(ue(GLOBALS.confDir), "\\.conf$", false, false, configMgrCallback, this);
    sort(configs.begin(), configs.end());
    
    if (configs.size() == 1)
        defaultConfig = configs[0].settings[sTitle].value;
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
    processDirectory(GLOBALS.cacheDir, "/\\w{32}$", false, false, housekeepingCallback, &configs, 1);
}
