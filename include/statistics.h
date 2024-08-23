
#ifndef STATISTICS_H
#define STATISTICS_H

#include <vector>
#include <set>

#include "globals.h"
#include "ConfigManager.h"


struct headerType {
    string name;
    unsigned long maxLength;
    bool override;
    
    headerType(string n, unsigned long m = 0) : name(n), maxLength(m ? m: (int)n.length()), override(false) {};
    void setMaxLength(unsigned long m) { maxLength = max(maxLength, m); }
};


struct detailType {
    long daily;
    long weekly;
    long monthly;
    long yearly;
    size_t bytesUsed;
    size_t bytesSaved;
    vector<string> ages;
    set<string> dayUnique;
    detailType() : daily(0), weekly(0), monthly(0), yearly(0), bytesUsed(0), bytesSaved(0) {};
};


void produceSummaryStatsWrapper(ConfigManager& configManager, int statDetail, bool cacheOnly = false);
void produceDetailedStats(ConfigManager& configManager, int statDetail);

#endif

