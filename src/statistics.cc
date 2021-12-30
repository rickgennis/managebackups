
#include <algorithm>

#include "statistics.h"
#include "util.h"

using namespace std;


void displayStatsForConfig(BackupConfig& config) {
    unsigned long fnameLen = 0;
    unsigned long sizeLen = 0;

    for (auto raw_it = config.cache.rawData.begin(); raw_it != config.cache.rawData.end(); ++raw_it) {
        fnameLen = max(fnameLen, raw_it->second.filename.length());
        sizeLen = max(sizeLen, raw_it->second.size);
    }
    sizeLen = to_string(sizeLen).length();

    auto fnameIdx = config.cache.indexByFilename;
    for (auto backup_it = fnameIdx.begin(); backup_it != fnameIdx.end(); ++backup_it) {
        auto raw_it = config.cache.rawData.find(backup_it->second);
        if (raw_it != config.cache.rawData.end()) {
            char result[500];
            sprintf(result, string(string("%-") + to_string(fnameLen+1) + "s  %" + to_string(sizeLen) + "i").c_str(),
                        raw_it->second.filename.c_str(), raw_it->second.size);
            cout << result << endl;

            //cout << onevarsprintf("%-" + to_string(fnameLen + 3) + "s", raw_it->second.filename) << endl;
        }
    }
}


void displayStats(ConfigManager& configManager) {
    if (configManager.activeConfig > -1)
        displayStatsForConfig(configManager.configs[configManager.activeConfig]);
    else {
        for (auto cfg_it = configManager.configs.begin(); cfg_it != configManager.configs.end(); ++cfg_it)
            displayStatsForConfig(*cfg_it);
    }
}


