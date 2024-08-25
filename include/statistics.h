
#ifndef STATISTICS_H
#define STATISTICS_H

#include <vector>
#include <set>

#include "globals.h"
#include "ConfigManager.h"

struct headerType {
    string name;
    long maxLength;
    bool defaultHide;
    bool overrideHide;

    /* header length (m)
          x to specify length of the field (will be padded on output)
          -1 to specify to hide the field unless override is later set (for conditionally displayed fields)
          unspecified to set length of field to length of header
     */
    headerType(string n, long m = 0) : name(n), maxLength(m > 0 ? m : (int)n.length()), defaultHide(m == -1), overrideHide(false) {};
};

class headerManager {
    vector<headerType> headers;
    
public:
    headerManager(const initializer_list<headerType>& list);
    
    void setMaxLength(int idx, long m, bool override = false);
    void override(int idx) { headers[idx].overrideHide = true; }
    string display(string monthYear = "", bool returnOnly = false);
    bool visible(int idx) { return (!headers[idx].defaultHide || headers[idx].overrideHide); }
    headerType operator[](int idx) { return headers[idx]; }
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

