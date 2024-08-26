
#ifndef STATISTICS_H
#define STATISTICS_H

#include <vector>
#include <set>
#include <deque>

#include "globals.h"
#include "ConfigManager.h"

struct headerType {
    string name;
    long maxLength;
    bool leftJustify;

    /* header length (m)
          x to specify length of the field (will be padded on output)
          -1 to specify to hide the field unless override is later set (for conditionally displayed fields)
          0 or unspecified to set length of field to length of header
     */
    headerType(string n, long m = 0, bool left = false) : name(n), maxLength(m), leftJustify(left) {};
    bool visible() { return maxLength; }
    void setMax(long m);

};

typedef vector<string> tableRow;

class tableManager {
    vector<headerType> headers;
    string row;
    int index;
    
public:
    tableManager(const initializer_list<headerType>& list);
    
    // Headers
    string displayHeader(string monthYear = "", bool returnOnly = false);
    headerType& operator[](int idx) { return headers[idx]; }
    
    // Rows
    void addRowData(string row);
    string displayRow();
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

