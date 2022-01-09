
#include <set>
#include <vector>
#include <algorithm>
#include <libgen.h>

#include "statistics.h"
#include "util_generic.h"
#include "globals.h"
#include "colors.h"

using namespace std;

string mtime2MY(time_t mtime) {
    char timeBuf[200];
    struct tm *tM = localtime(&mtime);
    struct tm fileTime = *tM;
    strftime(timeBuf, 200, "%B %Y", &fileTime);
    return(timeBuf);
}


void display1LineForConfig(BackupConfig& config) {
    double bytesUsed = 0;
    double bytesSaved = 0;
    set<unsigned long> countedInode;

    unsigned long rawSize = config.cache.rawData.size();
    if (rawSize < 1)
        return;

    // calcuclate stats from the entire list of backups
    for (auto raw_it = config.cache.rawData.begin(); raw_it != config.cache.rawData.end(); ++raw_it) {

        // calculate total bytes used and saved
        if (countedInode.find(raw_it->second.inode) == countedInode.end()) {
            countedInode.insert(raw_it->second.inode);
            bytesUsed += raw_it->second.size;
        }
        else
            bytesSaved += raw_it->second.size;
    }

    // calcuclate percentage saved
    int saved = floor((1 - double(bytesUsed / (bytesUsed + bytesSaved))) * 100 + 0.5);

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
            approximate(bytesUsed) + ")").c_str(),
            seconds2hms(last_it->second.duration).c_str(),
            rawSize,
            saved,
            string(timeDiff(first_it->second.name_mtime) + BOLDBLUE + " -> " + RESET + timeDiff(last_it->second.mtime).c_str()).c_str());
            cout << result << "\n";
    }
}


void displayStatsForConfig(BackupConfig& config) {
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
    int saved = floor((1 - double(bytesUsed / (bytesUsed + bytesSaved))) * 100 + 0.5);

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
            string monthYear = mtime2MY(raw_it->second.mtime);

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
            bool prectime = raw_it->second.links == 1 || !raw_it->second.day_age;

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
                        raw_it->second.links, timeDiff(prectime ? raw_it->second.mtime : raw_it->second.name_mtime).c_str());

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


void display1LineStats(ConfigManager& configManager) {
    cout << BOLDBLUE << "Most Recent Backup                        Size (Total)     Duration  Num  Saved  Age Range\n" << RESET;
    if (configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp)
        display1LineForConfig(configManager.configs[configManager.activeConfig]);
    else {
        for (auto cfg_it = configManager.configs.begin(); cfg_it != configManager.configs.end(); ++cfg_it) {
            if (!cfg_it->temp)
                display1LineForConfig(*cfg_it);
        }
    }
}


