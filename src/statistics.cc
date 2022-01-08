
#include <set>
#include <vector>
#include <algorithm>

#include "statistics.h"
#include "util_generic.h"
#include "globals.h"
#include "colors.h"

using namespace std;


void displayStatsForConfig(BackupConfig& config) {
    unsigned long fnameLen = 0;
    double totalBytesUsed = 0;
    double totalBytesSaved = 0;
    set<unsigned long> countedInode;

    // calcuclate stats from the entire list of backups
    for (auto raw_it = config.cache.rawData.begin(); raw_it != config.cache.rawData.end(); ++raw_it) {

        // track the length of the longest filename for formatting
        fnameLen = max(fnameLen, raw_it->second.filename.length());

        // calculate total bytes used and saved
        if (countedInode.find(raw_it->second.inode) == countedInode.end()) {
            countedInode.insert(raw_it->second.inode);
            totalBytesUsed += raw_it->second.size;
        }
        else
            totalBytesSaved += raw_it->second.size;
    }

    // calcuclate percentage saved
    int saved = floor((1 - double(totalBytesUsed / (totalBytesUsed + totalBytesSaved))) * 100 + 0.5);

    // make a pretty line
    char dash[5];
    sprintf(dash, "\u2501");
    string line;
    for (int x = 0; x < 60; ++x)
        line += dash;
        
    // print top summary of backups
    unsigned long rawSize = config.cache.rawData.size();
    unsigned long md5Size = config.cache.indexByMD5.size();
    cout << line << "\n";
    if (config.settings[sTitle].value.length()) cout << "Title: " << config.settings[sTitle].value << "\n";
    cout << "Directory: " << config.settings[sDirectory].value << " (" << config.settings[sBackupFilename].value << ")\n";
    cout << rawSize << " backup" << s(rawSize) << ", " << md5Size << " unique" << s(md5Size) << "\n";
    cout << approximate(totalBytesUsed + totalBytesSaved) << " using " << approximate(totalBytesUsed) << " on disk (saved " << saved << "%)\n";
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

            // extract the month and year from the mtime
            char timeBuf[200];
            struct tm *tM = localtime(&raw_it->second.mtime);
            struct tm fileTime = *tM;
            strftime(timeBuf, 200, "%B %Y", &fileTime);
            string monthYear = timeBuf;

            // print the month header
            if (lastMonthYear != monthYear) 
                cout << endl << BOLDBLUE << onevarsprintf("%-" + to_string(fnameLen+1) + "s  ", monthYear) <<
                    "Size    Duration  Type  Lnks  Age" << RESET << endl;

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


void displayStats(ConfigManager& configManager) {
    if (configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp)
        displayStatsForConfig(configManager.configs[configManager.activeConfig]);
    else {
        bool previous = false;
        for (auto cfg_it = configManager.configs.begin(); cfg_it != configManager.configs.end(); ++cfg_it) {
            if (!cfg_it->temp) {

                if (previous)
                    cout << "\n\n";

                displayStatsForConfig(*cfg_it);
                previous = true;
            }
        }
    }
}


