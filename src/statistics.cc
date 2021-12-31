
#include <set>
#include <algorithm>

#include "statistics.h"
#include "util.h"
#include "globals.h"
#include "colors.h"

using namespace std;


void displayStatsForConfig(BackupConfig& config) {
    unsigned long fnameLen = 0;
    set<string> colors { { GREEN, MAGENTA, CYAN, BLUE, YELLOW, BOLDGREEN, BOLDMAGENTA, BOLDYELLOW, BOLDCYAN } };
    auto currentColor_it = ++colors.begin();
    string lastMD5;

    for (auto raw_it = config.cache.rawData.begin(); raw_it != config.cache.rawData.end(); ++raw_it) {
        fnameLen = max(fnameLen, raw_it->second.filename.length());
    }

    // loop through the list of backups via the filename cache
    string lastMonthYear;
    auto fnameIdx = config.cache.indexByFilename;
    for (auto backup_it = fnameIdx.begin(); backup_it != fnameIdx.end(); ++backup_it) {

        // lookup the the raw data detail
        auto raw_it = config.cache.rawData.find(backup_it->second);
        if (raw_it != config.cache.rawData.end()) {

            // extract the month and year from the mtime
            char timeBuf[200];
            struct tm *tM = localtime(&raw_it->second.mtime);
            struct tm fileTime = *tM;
            strftime(timeBuf, 200, "%B %Y", &fileTime);
            string monthYear = timeBuf;

            // print the month header
            if (lastMonthYear != monthYear) 
                cout << endl << ifcolor(BOLDBLUE) << onevarsprintf("%-" + to_string(fnameLen+1) + "s  ", monthYear) <<
                    "Size     Duration  Type  Lnks  Age" << ifcolor(RESET) << endl;

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
                        raw_it->second.links, timeDiff(raw_it->second.mtime).c_str());

            // if there's more than 1 file with this MD5 then color code it as a set; otherwise no color
            if (config.cache.getByMD5(raw_it->second.md5).size() > 1) {

                // rotate the color if we're on a new set (i.e. new md5)
                if (lastMD5.length() && lastMD5 != raw_it->second.md5) {
                    ++currentColor_it;
                    if (currentColor_it == colors.end())
                        currentColor_it = ++colors.begin();
                }

                cout << ifcolor(*currentColor_it);
            }
            else
                cout << ifcolor(RESET);

            // print it out
            cout << result << endl;

            lastMD5 = raw_it->second.md5;
            lastMonthYear = monthYear;
        }
    }

    cout << ifcolor(RESET);
}


void displayStats(ConfigManager& configManager) {
    if (configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp)
        displayStatsForConfig(configManager.configs[configManager.activeConfig]);
    else {
        for (auto cfg_it = configManager.configs.begin(); cfg_it != configManager.configs.end(); ++cfg_it) {
            if (!cfg_it->temp)
                displayStatsForConfig(*cfg_it);
        }
    }
}


