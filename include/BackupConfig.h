
#ifndef BACKUPCONFIG_H
#define BACKUPCONFIG_H

#include <vector>
#include <variant>
#include <string>
#include <pcre++.h>
#include "Setting.h"
#include "BackupCache.h"
#include "FaubCache.h"


using namespace std;
using namespace pcrepp;


class BackupConfig {
    
public:
    string config_filename;
    
    bool modified;
    bool temp;
    vector<Setting> settings;
    BackupCache cache;
    FaubCache fcache;
    
    BackupConfig(bool makeTemp = 0);
    ~BackupConfig();
    
    bool isFaub() { return settings[sFaub].value.length(); };
    
    void fullDump();
    
    size_t getRecentAvgSize(int maxBackups = 10);
    size_t getBloatTarget();
    
    void saveConfig();
    bool loadConfig(string filename);
    void loadConfigsCache();
    
    void setPreviousFailures(unsigned int count);
    unsigned int getPreviousFailures();
    
    string setLockPID(unsigned int pid);
    tuple<int, time_t> getLockPID();
    
    string ifTitle();
    unsigned int removeEmptyDirs(string dir = "", int baseSlashes = 0);
    void renameBaseDirTo(string newBaseDir);
    
    friend bool operator<(const BackupConfig &b1, const BackupConfig &b2);
    friend bool operator>(const BackupConfig &b1, const BackupConfig &b2);
    friend bool operator==(const BackupConfig &b1, const BackupConfig &b2);
};

#endif

