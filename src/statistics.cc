
#include <set>
#include <vector>
#include <algorithm>
#include <libgen.h>
#include <math.h>
#include <sys/stat.h>

#include "statistics.h"
#include "util_generic.h"
#include "globals.h"
#include "colors.h"

#define NUMSTATDETAILS        10 

using namespace std;

struct summaryStats {
    bool success;
    bool inProcess;
    unsigned long lastBackupBytes;
    unsigned long totalBytesUsed;
    unsigned long totalBytesSaved;
    long numberOfBackups;
    unsigned long duration;
    string stringOutput[NUMSTATDETAILS];

    summaryStats() {
        success = inProcess = false;
        lastBackupBytes = totalBytesUsed = totalBytesSaved = numberOfBackups = duration = 0;
    }
};


summaryStats _displaySummaryStats(BackupConfig& config, int statDetail = 0, int maxFileLen = 0, int maxProfLen = 0) {
    set<unsigned long> countedInode;
    struct summaryStats resultStats;
    int precisionLevel = statDetail > 1 ? 1 : -1;

    unsigned long rawSize = config.cache.rawData.size();
    if (rawSize < 1) {
        return resultStats;
    }

    // calcuclate stats from the entire list of backups
    for (auto &raw: config.cache.rawData) {

        // calculate total bytes used and saved
        if (countedInode.find(raw.second.inode) == countedInode.end()) {
            countedInode.insert(raw.second.inode);
            resultStats.totalBytesUsed += raw.second.size;
        }
        else
            resultStats.totalBytesSaved += raw.second.size;
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
        string processAge;
        struct stat statBuf;

 #ifdef __APPLE__
        if (config.cache.inProcess.length() && !stat(config.cache.inProcess.c_str(), &statBuf))
            processAge = seconds2hms(GLOBALS.startupTime - statBuf.st_birthtime);
#endif

        auto t = localtime(&last_it->second.mtime);
        char fileTime[20];
        strftime(fileTime, sizeof(fileTime), "%X", t);

        string soutput[NUMSTATDETAILS] = { 
            config.settings[sTitle].value,
            pathSplit(last_it->second.filename).file,
            fileTime,    
            (approximate(last_it->second.size, precisionLevel, statDetail > 2) + " (" + approximate(resultStats.totalBytesUsed, precisionLevel, statDetail == 3) + ")"),
            seconds2hms(last_it->second.duration),
            to_string(rawSize),
            to_string(saved) + "%",
            timeDiff(mktimeval(first_it->second.name_mtime)),
            timeDiff(mktimeval(last_it->second.mtime)),
            processAge };
            
        for (int i = 0; i < NUMSTATDETAILS; ++i)
            resultStats.stringOutput[i] = soutput[i];

        resultStats.inProcess = config.cache.inProcess.length();
        resultStats.duration = last_it->second.duration;
        resultStats.lastBackupBytes = last_it->second.size;
    }

    resultStats.success = true;
    return resultStats;
}


void displaySummaryStatsWrapper(ConfigManager& configManager, int statDetail) {
    int maxFileLen = 0;
    int maxProfLen = 0;
    int nonTempConfigs = 0;
    int precisionLevel = statDetail > 1 ? 1 : -1;
    struct summaryStats perStats;
    struct summaryStats totalStats;
    vector<string> statStrings;
    vector<bool> profileInProcess;
    bool singleConfig = configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp;

    // calculate totals
    if (singleConfig) {
        perStats = _displaySummaryStats(configManager.configs[configManager.activeConfig], statDetail, maxFileLen, maxProfLen);
        profileInProcess.insert(profileInProcess.end(), perStats.inProcess);

        for (int i = 0; i < NUMSTATDETAILS; ++i) 
            statStrings.insert(statStrings.end(), perStats.stringOutput[i]);
    }
    else {
        for (auto &config: configManager.configs) 
            if (!config.temp) {
                ++nonTempConfigs;
                perStats = _displaySummaryStats(config, statDetail, maxFileLen, maxProfLen);
                totalStats.lastBackupBytes += perStats.lastBackupBytes;
                totalStats.totalBytesUsed += perStats.totalBytesUsed;
                totalStats.totalBytesSaved += perStats.totalBytesSaved;
                totalStats.numberOfBackups += perStats.numberOfBackups;
                totalStats.duration += perStats.duration;

                profileInProcess.insert(profileInProcess.end(), perStats.inProcess);

                for (int i = 0; i < NUMSTATDETAILS; ++i)
                    statStrings.insert(statStrings.end(), perStats.stringOutput[i]);
            }
    }

        // add totals into totalStats
        if (!singleConfig) {
            statStrings.insert(statStrings.end(), "");
            statStrings.insert(statStrings.end(), "");
            statStrings.insert(statStrings.end(), "");
            statStrings.insert(statStrings.end(), approximate(totalStats.lastBackupBytes, precisionLevel, statDetail > 2) + " (" +
                approximate(totalStats.totalBytesUsed, precisionLevel, statDetail > 2) + ")");
            statStrings.insert(statStrings.end(), seconds2hms(totalStats.duration));
            statStrings.insert(statStrings.end(), to_string(totalStats.numberOfBackups));
            statStrings.insert(statStrings.end(), to_string(int(floor((1 - ((long double)totalStats.totalBytesUsed / ((long double)totalStats.totalBytesUsed + (long double)totalStats.totalBytesSaved))) * 100 + 0.5))) + "%");
            statStrings.insert(statStrings.end(), totalStats.totalBytesSaved ? string(string("Saved ") + approximate(totalStats.totalBytesSaved, precisionLevel, statDetail > 2) + " from "
                + approximate(totalStats.totalBytesUsed + totalStats.totalBytesSaved, precisionLevel, statDetail > 2)) : "");
            statStrings.insert(statStrings.end(), "");
            statStrings.insert(statStrings.end(), "");
        }

        // determine the longest length entry of each column to allow consistent horizontal formatting
        int colLen[NUMSTATDETAILS] = {};
        int numberStatStrings = statStrings.size();
        for (int column = 0; column < NUMSTATDETAILS - 1; ++column) {
            int line = 0;

            while (NUMSTATDETAILS * line + column < numberStatStrings) {
                if (column == 7)  // cols 7 and 8 get combined
                    colLen[column] = max(colLen[column],
                            statStrings[NUMSTATDETAILS * line + column].length() +
                            statStrings[NUMSTATDETAILS * line + column+1].length() + 6);
                else if (column > 7)
                    colLen[column] = max(colLen[column], statStrings[NUMSTATDETAILS * line + column+1].length());
                else
                    colLen[column] = max(colLen[column], statStrings[NUMSTATDETAILS * line + column].length());

                ++line;
            }
        }

        // print the header row
        // the blank at the end isn't just for termination; it's used for "in process" status
        string headers[] = { "Profile", "Most Recent Backup", "Finished", "Size (Total)", "Duration", "Num", "Saved", "Age Range", "" };
        cout << BOLDBLUE;

        int x = -1;
        while (headers[++x].length()) {
            cout << (x == 0 ? "" : "  ") << headers[x];

            // pad the headers to line up with the longest item in each column
            if (colLen[x] > headers[x].length()) {
                string spaces(colLen[x] - headers[x].length(), ' ');
                cout << spaces;
            }
        }
        cout << RESET << "\n";

        // setup line formatting
        string lineFormat;
        for (int x = 0; x < NUMSTATDETAILS - 1; ++x)
            if (max(headers[x].length(), colLen[x]))
                lineFormat += (lineFormat.length() ? "  " : "") + string("%") + string(x == 6 ? "" : "-") + to_string(max(headers[x].length(), colLen[x])) + "s";   // 6th column is right-justified

        // print line by line results
        char result[1000];
        int line = 0;
        while (line * NUMSTATDETAILS < numberStatStrings - (singleConfig ? 0 : NUMSTATDETAILS)) {
            string HIGHLIGHT = profileInProcess[line] ? BOLDGREEN : BOLDBLUE;
            string BRACKETO = profileInProcess[line] ? "{" : "[";
            string BRACKETC = profileInProcess[line] ? "}" : "]";

            sprintf(result, lineFormat.c_str(), 
                    statStrings[line * NUMSTATDETAILS].c_str(),
                    statStrings[line * NUMSTATDETAILS + 1].c_str(),
                    statStrings[line * NUMSTATDETAILS + 2].c_str(),
                    statStrings[line * NUMSTATDETAILS + 3].c_str(),
                    statStrings[line * NUMSTATDETAILS + 4].c_str(),
                    statStrings[line * NUMSTATDETAILS + 5].c_str(),
                    statStrings[line * NUMSTATDETAILS + 6].c_str(),
                    string(HIGHLIGHT + BRACKETO + RESET + statStrings[line * NUMSTATDETAILS + 7] +
                        HIGHLIGHT + string(" -> ") + RESET + statStrings[line * NUMSTATDETAILS + 8] + 
                        HIGHLIGHT + BRACKETC).c_str(),
                    string(statStrings[line * NUMSTATDETAILS + 9]).c_str());
            cout << result << RESET << "\n";
            ++line;
        }

        // print the totals line
        if (!singleConfig && nonTempConfigs > 1) {
            sprintf(result, string(
                    "%-" + to_string(max(headers[3].length(), colLen[3])) + "s  " +
                    "%-" + to_string(max(headers[4].length(), colLen[4])) + "s  " +
                    "%-" + to_string(max(headers[5].length(), colLen[5])) + "s  " +
                    "%"  + to_string(max(headers[6].length(), colLen[6])) + "s  " +
                    "%-" + to_string(colLen[7]) + "s").c_str(),
                    statStrings[line * NUMSTATDETAILS + 3].c_str(),
                    statStrings[line * NUMSTATDETAILS + 4].c_str(),
                    statStrings[line * NUMSTATDETAILS + 5].c_str(),
                    statStrings[line * NUMSTATDETAILS + 6].c_str(),
                    statStrings[line * NUMSTATDETAILS + 7].c_str(),
                    statStrings[line * NUMSTATDETAILS + 8].c_str());

            string spaces(max(headers[0].length(), colLen[0]) + max(headers[1].length(), colLen[1]) + colLen[2], ' ');
            cout << BOLDWHITE << "TOTALS" << spaces << result << RESET << "\n";
        }

    return;
}


void _displayDetailedStats(BackupConfig& config) {
    unsigned long fnameLen = 0;
    double bytesUsed = 0;
    double bytesSaved = 0;
    set<unsigned long> countedInode;

    // calcuclate stats from the entire list of backups
    for (auto &raw: config.cache.rawData) {

        // track the length of the longest filename for formatting
        fnameLen = max(fnameLen, raw.second.filename.length());

        // calculate total bytes used and saved
        if (countedInode.find(raw.second.inode) == countedInode.end()) {
            countedInode.insert(raw.second.inode);
            bytesUsed += raw.second.size;
        }
        else
            bytesSaved += raw.second.size;
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
    for (auto &backup: fnameIdx) {

        // lookup the the raw data detail
        auto raw_it = config.cache.rawData.find(backup.second);
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
        for (auto &config: configManager.configs) {
            if (!config.temp) {

                if (previous)
                    cout << "\n\n";

                _displayDetailedStats(config);
                previous = true;
            }
        }
    }
}

