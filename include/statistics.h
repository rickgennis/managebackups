
#ifndef STATISTICS_H
#define STATISTICS_H

#include "globals.h"
#include "ConfigManager.h"


struct headerType {
    string name;
    unsigned long maxLength;
    bool override;
    
    headerType(string n, unsigned long m = 0) : name(n), maxLength(m ? m: (int)n.length()), override(false) {};
    void setMaxLength(unsigned long m) { maxLength = max(maxLength, m); }
};


void displayDetailedStatsWrapper(ConfigManager& configManager, int statDetail);
void displaySummaryStatsWrapper(ConfigManager& configManager, int statDetail, bool cacheOnly = false);

#endif

