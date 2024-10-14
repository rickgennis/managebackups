
#ifndef CONFIGMANAGER
#define CONFIGMANAGER

#include <vector>
#include <string>
#include <tuple>
#include "BackupConfig.h"

using namespace std;


class ConfigManager {
public:
    int activeConfig;
    string defaultConfig;
    vector<BackupConfig> configs;
    int findConfig(string title);
    
    void fullDump();
    void loadAllConfigCaches();
    void housekeeping();
    
    void tagBackup(string tagname, string backup);
    string holdBackup(string hold, string backup, bool briefOutput = false);
    
    int numberActiveLocks();

    ConfigManager();
};

#endif
