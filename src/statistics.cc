
#include <set>
#include <vector>
#include <algorithm>
#include <libgen.h>
#include <math.h>

#include "statistics.h"
#include "util_generic.h"
#include "globals.h"
#include "colors.h"

using namespace std;

struct summaryStats {
    bool success;
    unsigned long lastBackupBytes;
    unsigned long totalBytesUsed;
    unsigned long totalBytesSaved;
    long numberOfBackups;
    unsigned long duration;

    summaryStats() {
        success = false;
        lastBackupBytes = totalBytesUsed = totalBytesSaved = numberOfBackups = duration = 0;
    }
};


summaryStats _displaySummaryStats(BackupConfig& config) {
    set<unsigned long> countedInode;
    struct summaryStats resultStats;

    unsigned long rawSize = config.cache.rawData.size();
    if (rawSize < 1) {
        return resultStats;
    }

    // calcuclate stats from the entire list of backups
    for (auto raw_it = config.cache.rawData.begin(); raw_it != config.cache.rawData.end(); ++raw_it) {

        // calculate total bytes used and saved
        if (countedInode.find(raw_it->second.inode) == countedInode.end()) {
            countedInode.insert(raw_it->second.inode);
            resultStats.totalBytesUsed += raw_it->second.size;
        }
        else
            resultStats.totalBytesSaved += raw_it->second.size;
    }
    resultStats.numberOfBackups = config.cache.rawData.size();

    // calcuclate percentage saved
    int saved = floor((1 - ((long double)resultStats.totalBytesUsed / ((long double)resultStats.totalBytesUsed + (long double)resultStats.totalBytesSaved))) * 100 + 0.5);

    auto firstEntry = config.cache.indexByFilename.begin();
    auto lastEntry = config.cache.indexByFilename.end();
    --lastEntry;
    
    auto first_it = config.cache.rawData.find(firstEntry->second);
    auto last_it = config.cache.rawData.find(lastEntry->second);
    if (last_it != config.cache.rawData.end() && first_it != config.cache.rawData.end()) {
        char result[1000];
        sprintf(result,
            // filename
            string(string("%-") + to_string(40) + "s  " +
            // size
            "%-15s  " +
            // duration
            "%s  " +
            // number 
            "%-4u  " +
            // saved
            "%3i%%  "+
            // content age
            "%s").c_str(),
            pathSplit(last_it->second.filename).file.c_str(), (approximate(last_it->second.size) + " (" +
            approximate(resultStats.totalBytesUsed) + ")").c_str(),
            seconds2hms(last_it->second.duration).c_str(),
            rawSize,
            saved,
            config.cache.inProcess ? 
                string(BOLDGREEN + string("{") + RESET + timeDiff(mktimeval(first_it->second.name_mtime)) + BOLDGREEN + " -> " + 
                    RESET + timeDiff(mktimeval(last_it->second.mtime)).c_str() + BOLDGREEN + "}" + RESET).c_str()
                : string(BOLDBLUE + string("[") + RESET + timeDiff(mktimeval(first_it->second.name_mtime)) + BOLDBLUE + " -> " + 
                    RESET + timeDiff(mktimeval(last_it->second.mtime)).c_str() + BOLDBLUE + "]" + RESET).c_str());
            cout << result << "\n";

            resultStats.duration = last_it->second.duration;
            resultStats.lastBackupBytes = last_it->second.size;
    }

    resultStats.success = true;
    return resultStats;
}


void displaySummaryStatsWrapper(ConfigManager& configManager) {
    cout << BOLDBLUE << "Most Recent Backup                        Size (Total)     Duration  Num  Saved  Age Range\n" << RESET;
    if (configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp)
        _displaySummaryStats(configManager.configs[configManager.activeConfig]);
    else {
        struct summaryStats perStats;
        struct summaryStats totalStats;

        int nonTempConfigs = 0;
        for (auto cfg_it: configManager.configs) {
            if (!cfg_it.temp) {
                ++nonTempConfigs;
                perStats = _displaySummaryStats(cfg_it);
                totalStats.lastBackupBytes += perStats.lastBackupBytes;
                totalStats.totalBytesUsed += perStats.totalBytesUsed;
                totalStats.totalBytesSaved += perStats.totalBytesSaved;
                totalStats.numberOfBackups += perStats.numberOfBackups;
                totalStats.duration += perStats.duration;
            }
        }

        if (nonTempConfigs > 1) {
            //cout << horizontalLine(80) << "\n";
            char result[1000];

            sprintf(result,
            // size
            string(string("%-15s  ") +
            // duration
            "%s  " +
            // number
            "%-4ld  " +
            // saved
            "%3i%%  "+
            // content age
            "%s").c_str(),

            (approximate(totalStats.lastBackupBytes) + " (" +
                approximate(totalStats.totalBytesUsed) + ")").c_str(),
            seconds2hms(totalStats.duration).c_str(),
            totalStats.numberOfBackups,
            int(floor((1 - ((long double)totalStats.totalBytesUsed / ((long double)totalStats.totalBytesUsed + (long double)totalStats.totalBytesSaved))) * 100 + 0.5)),
            totalStats.totalBytesSaved ? string(string("Would have taken ") + approximate(totalStats.totalBytesUsed + totalStats.totalBytesSaved)).c_str() : "");
            cout << BOLDWHITE << "TOTALS                                    " << result << RESET << "\n";
        }
    }

    return;
}


void _displayDetailedStats(BackupConfig& config) {
    unsigned long fnameLen = 0;
    double bytesUsed = 0;
    double bytesSaved = 0;
    set<unsigned long> countedInode;

    // calcuclate stats from the entire list of backups
    for (auto raw_it = config.cache.rawData.begin(); raw_it != config.cache.rawData.end(); ++raw_it) {

        // track the length of the longest filename for formatting
        fnameLen = max(fnameLen, raw_it->second.filename.length());

        // calculate total bytes used and saved
        if (countedInode.find(raw_it->second.inode) == countedInode.end()) {
            countedInode.insert(raw_it->second.inode);
            bytesUsed += raw_it->second.size;
        }
        else
            bytesSaved += raw_it->second.size;
    }

    // calcuclate percentage saved
    int saved = floor((1 - (long double)(bytesUsed / (bytesUsed + bytesSaved))) * 100 + 0.5);

    auto line = horizontalLine(60);
        
    // print top summary of backups
    unsigned long rawSize = config.cache.rawData.size();
    unsigned long md5Size = config.cache.indexByMD5.size();
    cout << line << "\n";
    if (config.settings[sTitle].value.length()) cout << "Profile: " << config.settings[sTitle].value << "\n";
    cout << "Directory: " << config.settings[sDirectory].value << " (" << config.settings[sBackupFilename].value << ")\n";
    cout << rawSize << " backup" << s(rawSize) << ", " << md5Size << " unique" << s(md5Size) << "\n";
    cout << approximate(bytesUsed + bytesSaved) << " using " << approximate(bytesUsed) << " on disk (saved " << saved << "%)\n";
    cout << line << endl;

    string lastMonthYear;
    string lastMD5;
    vector<string> colors { { GREEN, MAGENTA, CYAN, BLUE, YELLOW, BOLDGREEN, BOLDMAGENTA, BOLDYELLOW, BOLDCYAN } };
    auto currentColor_it = colors.begin()++;
    auto fnameIdx = config.cache.indexByFilename;

    // loop through the list of backups via the filename cache
    for (auto backup_it = fnameIdx.begin(); backup_it != fnameIdx.end(); ++backup_it) {

        // lookup the the raw data detail
        auto raw_it = config.cache.rawData.find(backup_it->second);
        if (raw_it != config.cache.rawData.end()) {
            string monthYear = vars2MY(raw_it->second.date_month, raw_it->second.date_year);

            // print the month header
            if (lastMonthYear != monthYear) 
                cout << endl << BOLDBLUE << onevarsprintf("%-" + to_string(fnameLen+1) + "s  ", monthYear) <<
                    "Size    Duration  Type  Lnks  Age" << RESET << endl;


            // file age can be calculated from the mtime which is an accurate number returned by
            // stat(), but in the case of multiple backups hardlinked together (due to identical content)
            // will actually be the mtime of the most recent of those files
            //      OR
            // file age can be calculaed from name_mtime, which is based on the date in the filename rounded
            // to midnight.
            //
            // precisetime (prectime) says use the filesystem mtime if the number of hard links is 1 (i.e.
            // only one backup using that inode and mtime) or if the file is less than a day old.  then we
            // get precision.  if more than one file shares that inode (links > 1) and the file older than
            // today, revert to the midnight rounded date derived from the filename.
            //
            // if the mtime and the name_mtime refer to completely different days then go non-precise and use
            // the name_mtime.
            bool prectime = mtimesAreSameDay(raw_it->second.mtime, raw_it->second.name_mtime) && 
                (raw_it->second.links == 1 || !raw_it->second.day_age);

            // format the detail for output
            char result[1000];
            sprintf(result, 
                    // filename
                    string(string("%-") + to_string(fnameLen+1) + "s  " + 
                    // size
                    "%6s  " +
                    // duration
                    "%s  " +
                    // type
                    "%-4s  " +
                    // links
                    "%4u  " +
                    // content age
                    "%s").c_str(),
                        raw_it->second.filename.c_str(), approximate(raw_it->second.size).c_str(), 
                        seconds2hms(raw_it->second.duration).c_str(),
                        raw_it->second.date_day == 1 ? "Mnth" : raw_it->second.dow == 0 ? "Week" : "Day",
                        raw_it->second.links, timeDiff(mktimeval(prectime ? raw_it->second.mtime : raw_it->second.name_mtime)).c_str());

            // if there's more than 1 file with this MD5 then color code it as a set; otherwise no color
            if (config.cache.getByMD5(raw_it->second.md5).size() > 1) {

                // rotate the color if we're on a new set (i.e. new md5)
                if (lastMD5.length() && lastMD5 != raw_it->second.md5) {
                    ++currentColor_it;
                    if (currentColor_it == colors.end())
                        currentColor_it = ++colors.begin();
                }

                cout << *currentColor_it;
            }
            else
                cout << RESET;

            // print it out
            cout << result << endl;

            lastMD5 = raw_it->second.md5;
            lastMonthYear = monthYear;
        }
    }

    cout << RESET;
}


void displayDetailedStatsWrapper(ConfigManager& configManager) {
    if (configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp)
        _displayDetailedStats(configManager.configs[configManager.activeConfig]);
    else {
        bool previous = false;
        for (auto cfg_it: configManager.configs) {
            if (!cfg_it.temp) {

                if (previous)
                    cout << "\n\n";

                _displayDetailedStats(cfg_it);
                previous = true;
            }
        }
    }
}

