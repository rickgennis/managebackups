
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
    tuple<int, string> findConfig(string title);
    void fullDump();
    void loadAllConfigCaches();
    
    void housekeeping();
    
    ConfigManager();
};

#endif
