
#ifndef STATISTICS_H
#define STATISTICS_H

#include "globals.h"
#include "ConfigManager.h"

void displayDetailedStatsWrapper(ConfigManager& configManager, int statDetail);
void displaySummaryStatsWrapper(ConfigManager& configManager, int statDetail, bool cacheOnly = false);

#endif

