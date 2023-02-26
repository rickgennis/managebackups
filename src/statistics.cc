
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
#include "FaubCache.h"

#define NUMSTATDETAILS        10 

using namespace std;
string oldMessage = ">24H old";


/* The summaryStats structure is used to return stat totals on a given config to the calling function.
 * Some data is broken out into individual totals (the longs).  Other is returned as strings for
 * easy display (stringOutput).
 */
struct summaryStats {
    bool inProcess;
    unsigned long lastBackupBytes;
    time_t lastBackupTime;
    size_t totalUsed;
    size_t totalSaved;
    long numberOfBackups;
    long uniqueBackups;
    unsigned long duration;
    string stringOutput[NUMSTATDETAILS] = {"", "(no backups found)", "-", "00:00:00", "0", "0", "0%"};

    summaryStats() {
        inProcess = false;
        lastBackupBytes = totalUsed = totalSaved = numberOfBackups = uniqueBackups = duration = 0;
    }
};


summaryStats calculateSummaryStats(BackupConfig& config, int statDetail = 0) {
    set<ino_t> countedInode;
    struct summaryStats resultStats;
    int precisionLevel = statDetail > 1 ? 1 : -1;
    string processAge;

    // handle faub configs first
    if (config.settings[sFaub].value.length()) {
        FaubCache fcache(config.settings[sDirectory].value, config.settings[sTitle].value);
        if (fcache.size() < 1) {
            resultStats.stringOutput[0] = config.settings[sTitle].value;
            return resultStats;
        }

        string inProcessFilename = fcache.getInProcessFilename();

#ifdef __APPLE__
        struct stat statBuf;
        if (inProcessFilename.length() && !stat(inProcessFilename.c_str(), &statBuf))
            processAge = seconds2hms(GLOBALS.startupTime - statBuf.st_birthtime);
#endif

        // set numeric stats
        auto ds = fcache.getTotalStats();
        resultStats.totalUsed = ds.getSize();
        resultStats.totalSaved = ds.getSaved();
        resultStats.numberOfBackups = resultStats.uniqueBackups = fcache.size();
        resultStats.lastBackupBytes = fcache.getLastBackup()->second.ds.getSize() + fcache.getLastBackup()->second.ds.getSaved();
        resultStats.lastBackupTime = fcache.getLastBackup()->second.finishTime;
        resultStats.duration = fcache.getLastBackup()->second.duration;
        resultStats.inProcess = inProcessFilename.length() > 0;

        // set string stats
        auto t = localtime(&resultStats.lastBackupTime);
        char fileTime[20];
        strftime(fileTime, sizeof(fileTime), "%X", t);

        int saved = floor((1 - (long double)resultStats.totalUsed / ((long double)resultStats.totalUsed + (long double)resultStats.totalSaved)) * 100 + 0.5);

        string soutput[NUMSTATDETAILS] = { 
            config.settings[sTitle].value,
            pathSplit(fcache.getLastBackup()->first).file,
            fileTime,    
            seconds2hms(resultStats.duration),
            (approximate(resultStats.lastBackupBytes, precisionLevel, statDetail > 2) + 
             " (" + approximate(resultStats.totalUsed, precisionLevel, statDetail == 3) + ")"),
            to_string(resultStats.uniqueBackups),
            to_string(saved) + "%",
            fcache.getFirstBackup()->second.finishTime ? timeDiff(mktimeval(fcache.getFirstBackup()->second.finishTime)) : "?",
            fcache.getLastBackup()->second.finishTime ? timeDiff(mktimeval(fcache.getLastBackup()->second.finishTime)) : "?",
            processAge.length() ? processAge : GLOBALS.startupTime - fcache.getLastBackup()->second.finishTime > 2*60*60*24 ? oldMessage : ""
        };
            
        for (int i = 0; i < NUMSTATDETAILS; ++i)
            resultStats.stringOutput[i] = soutput[i];

        return resultStats;
    }

    // then regular configs
    resultStats.numberOfBackups = config.cache.rawData.size();
    if (resultStats.numberOfBackups < 1) {
        resultStats.stringOutput[0] = config.settings[sTitle].value;
        return resultStats;
    }

    // calcuclate stats from the entire list of backups
    for (auto &raw: config.cache.rawData) {

        // calculate total bytes used and saved
        if (countedInode.find(raw.second.inode) == countedInode.end()) {
            countedInode.insert(raw.second.inode);
            resultStats.totalUsed += raw.second.size;
        }
        else
            resultStats.totalSaved += raw.second.size;
    }
    resultStats.uniqueBackups = config.cache.indexByMD5.size();

    // calcuclate percentage saved
    int saved = floor((1 - ((long double)resultStats.totalUsed / ((long double)resultStats.totalUsed + (long double)resultStats.totalSaved))) * 100 + 0.5);

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
            seconds2hms(last_it->second.duration),
            (approximate(last_it->second.size, precisionLevel, statDetail > 2) + " (" + approximate(resultStats.totalUsed, precisionLevel, statDetail == 3) + ")"),
            (to_string(resultStats.uniqueBackups) + " (" + to_string(resultStats.numberOfBackups) + ")"),
            to_string(saved) + "%",
            first_it->second.name_mtime ? timeDiff(mktimeval(first_it->second.name_mtime)) : "?",
            last_it->second.mtime ? timeDiff(mktimeval(last_it->second.mtime)) : "?",
            processAge.length() ? processAge : GLOBALS.startupTime - last_it->second.name_mtime > 2*60*60*24 ? oldMessage : ""
        };
            
        for (int i = 0; i < NUMSTATDETAILS; ++i)
            resultStats.stringOutput[i] = soutput[i];

        resultStats.inProcess = config.cache.inProcess.length();
        resultStats.duration = last_it->second.duration;
        resultStats.lastBackupBytes = last_it->second.size;
        resultStats.lastBackupTime = last_it->second.mtime;
    }

    return resultStats;
}


void displaySummaryStatsWrapper(ConfigManager& configManager, int statDetail) {
    int nonTempConfigs = 0;
    int precisionLevel = statDetail > 1 ? 1 : -1;
    struct summaryStats perStats;
    struct summaryStats totalStats;
    vector<string> statStrings;
    vector<bool> profileInProcess;
    bool singleConfig = configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp;

    // calculate totals
    if (singleConfig) {
        perStats = calculateSummaryStats(configManager.configs[configManager.activeConfig], statDetail);
        profileInProcess.insert(profileInProcess.end(), perStats.inProcess);

        for (int i = 0; i < NUMSTATDETAILS; ++i)
            statStrings.insert(statStrings.end(), perStats.stringOutput[i]);
    }
    else {
        for (auto &config: configManager.configs) 
            if (!config.temp) {
                ++nonTempConfigs;
                perStats = calculateSummaryStats(config, statDetail);
                totalStats.lastBackupBytes += perStats.lastBackupBytes;
                totalStats.totalUsed += perStats.totalUsed;
                totalStats.totalSaved += perStats.totalSaved;
                totalStats.numberOfBackups += perStats.numberOfBackups;
                totalStats.uniqueBackups += perStats.uniqueBackups;
                totalStats.duration += perStats.duration;

                profileInProcess.insert(profileInProcess.end(), perStats.inProcess);

                for (int i = 0; i < NUMSTATDETAILS; ++i)
                    statStrings.insert(statStrings.end(), perStats.stringOutput[i]);
            }
    }

        // add totals into totalStats
        if (!singleConfig) {
            statStrings.insert(statStrings.end(), "");   // skip profile
            statStrings.insert(statStrings.end(), "");   // skip most recent backup
            statStrings.insert(statStrings.end(), "");   // skip finish time
            statStrings.insert(statStrings.end(), seconds2hms(totalStats.duration));
            statStrings.insert(statStrings.end(), approximate(totalStats.lastBackupBytes, precisionLevel, statDetail > 2) + " (" +
                approximate(totalStats.totalUsed, precisionLevel, statDetail > 2) + ")");
            statStrings.insert(statStrings.end(), to_string(totalStats.uniqueBackups) + " (" + to_string(totalStats.numberOfBackups) + ")");
            statStrings.insert(statStrings.end(), to_string(int(floor((1 - ((long double)totalStats.totalUsed / ((long double)totalStats.totalUsed + (long double)totalStats.totalSaved))) * 100 + 0.5))) + "%");
            statStrings.insert(statStrings.end(), totalStats.totalSaved ? string(string("Saved ") + approximate(totalStats.totalSaved, precisionLevel, statDetail > 2) + " from "
                + approximate(totalStats.totalUsed + totalStats.totalSaved, precisionLevel, statDetail > 2)) : "");
            statStrings.insert(statStrings.end(), "");
            statStrings.insert(statStrings.end(), "");
        }

        // determine the longest length entry of each column to allow consistent horizontal formatting
        int colLen[NUMSTATDETAILS] = {};
        int numberStatStrings = (int)statStrings.size();
        for (int column = 0; column < NUMSTATDETAILS - 1; ++column) {
            int line = 0;

            while (NUMSTATDETAILS * line + column < numberStatStrings) {
                if (column == 7)  // cols 7 and 8 get combined
                    colLen[column] = (int)max(colLen[column],
                            statStrings[NUMSTATDETAILS * line + column].length() +
                            statStrings[NUMSTATDETAILS * line + column+1].length() + 6);
                else if (column > 7)
                    colLen[column] = (int)max(colLen[column], statStrings[NUMSTATDETAILS * line + column+1].length());
                else
                    colLen[column] = (int)max(colLen[column], statStrings[NUMSTATDETAILS * line + column].length());

                ++line;
            }
        }

        // print the header row
        // the blank at the end isn't just for termination; it's used for "in process" status
        string headers[] = { "Profile", "Most Recent Backup", "Finish@", "Duration", "Size (Total)", "Uniq (T)", "Saved", "Age Range", "" };
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
                lineFormat += (lineFormat.length() ? "  " : "") + string("%") + string(x == 6 ? "" : "-") + 
                    to_string(max(headers[x].length(), colLen[x])) + "s";   // 6th column is right-justified

        // print line by line results
        char result[1000];
        int line = 0;
        while (line * NUMSTATDETAILS < numberStatStrings - (singleConfig ? 0 : NUMSTATDETAILS)) {
            string HIGHLIGHT = profileInProcess[line] ? BOLDGREEN : BOLDBLUE;
            string BRACKETO = profileInProcess[line] ? "{" : "[";
            string BRACKETC = profileInProcess[line] ? "}" : "]";
            string msg = statStrings[line * NUMSTATDETAILS + 9];
            bool is_old = msg == oldMessage;

            bool gotAge = statStrings[line * NUMSTATDETAILS + 7].length() || statStrings[line * NUMSTATDETAILS + 8].length();
            snprintf(result, sizeof(result), lineFormat.c_str(), 
                    statStrings[line * NUMSTATDETAILS].c_str(),
                    statStrings[line * NUMSTATDETAILS + 1].c_str(),
                    statStrings[line * NUMSTATDETAILS + 2].c_str(),
                    statStrings[line * NUMSTATDETAILS + 3].c_str(),
                    statStrings[line * NUMSTATDETAILS + 4].c_str(),
                    statStrings[line * NUMSTATDETAILS + 5].c_str(),
                    statStrings[line * NUMSTATDETAILS + 6].c_str(),
                    string(HIGHLIGHT + BRACKETO + RESET + 
                        (gotAge ? statStrings[line * NUMSTATDETAILS + 7] + HIGHLIGHT + 
                         string(" -> ") + RESET + statStrings[line * NUMSTATDETAILS + 8] : "-") + 
                        HIGHLIGHT + BRACKETC).c_str(),
                    string(is_old ? string(BOLDRED) + msg : msg).c_str());
            cout << result << RESET << "\n";
            ++line;
        }

        // print the totals line
        if (!singleConfig && nonTempConfigs > 1) {
            snprintf(result, sizeof(result), string(
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


bool _displayDetailedFaubStats(BackupConfig& config, int statDetail) {
    FaubCache fcache(config.settings[sDirectory].value, config.settings[sTitle].value);
    int precisionLevel = statDetail > 1 ? 1 : -1;

    if (config.settings[sFaub].value.length() && fcache.size()) {
        auto line = horizontalLine(60);
        auto bkups = fcache.getNumberOfBackups();
        auto stats = fcache.getTotalStats();
        int saved = floor((1 - (long double)stats.getSize() / (stats.getSize() + stats.getSaved())) * 100 + 0.5);

        // print top summary of backups
        cout << line << "\n";
        if (config.settings[sTitle].value.length()) cout << "Profile: " << config.settings[sTitle].value << "\n";
        cout << "Directory: " << config.settings[sDirectory].value << "\n";
        cout << bkups << " backup" << s((int)bkups) << "\n";
        cout << approximate(stats.getSize() + stats.getSaved()) << " using " << approximate(stats.getSize()) << " on disk (saved " << saved << "%)\n";
        
        // determine the backup max filename length and rention match counts
        set<string> dayUnique;
        struct tm *timeDetail;
        int fnameLen = 0, numDay = 0, numWeek = 0, numMonth = 0, numYear = 0;
        auto backupIt = fcache.getFirstBackup();
        while (backupIt != fcache.getEnd()) {
            fnameLen = (int)max(fnameLen, backupIt->first.length());
            timeDetail = localtime(&backupIt->second.finishTime);
            auto timeString = to_string(timeDetail->tm_year) + to_string(timeDetail->tm_mon) + to_string(timeDetail->tm_mday);
   
            if (dayUnique.find(timeString) == dayUnique.end()) {
                dayUnique.insert(timeString);
                if (timeDetail->tm_mon == 0 && timeDetail->tm_mday == 1)
                    ++numYear;
                else if (timeDetail->tm_mday == 1)
                    ++numMonth;
                else if (timeDetail->tm_wday == config.settings[sDOW].ivalue())
                    ++numWeek;
                else
                    ++numDay;
            }
            ++backupIt;
        }

        cout << "Retention stats:\n";
        cout << "\t• " << numDay << " of " << config.settings[sDays].ivalue() << " daily\n";
        cout << "\t• " << numWeek << " of " << config.settings[sWeeks].ivalue() << " weekly\n";
        cout << "\t• " << numMonth << " of " << config.settings[sMonths].ivalue() << " monthly\n";
        cout << "\t• " << numYear << " of " << config.settings[sYears].ivalue() << " yearly\n";
        cout << line << endl;

        string lastMonthYear;
        char result[1000];
        backupIt = fcache.getFirstBackup();
        while (backupIt != fcache.getEnd())  {
            timeDetail = localtime(&backupIt->second.finishTime);
            string monthYear = backupIt->second.finishTime ? vars2MY(timeDetail->tm_mon+1, timeDetail->tm_year+1900) : "Unknown";

            // print the month header
            if (lastMonthYear != monthYear)
                cout << endl << BOLDBLUE << onevarsprintf("%-" + to_string(fnameLen+1) + "s  ", monthYear) <<
                    "Size    Used    Dirs    SymLks  Mods    Duration  Type  Age" << RESET << endl;

            snprintf(result, sizeof(result), 
                    // filename
                    string(string("%-") + to_string(fnameLen+1) + "s  " +
                    // size
                    "%6s  " +
                    // used 
                    "%6s  " +
                    // dirs
                    "%6s  " +
                    // symlinks
                    "%6s  " +
                    // modifies
                    "%6s  " +
                    // duration
                    "%s  " +
                    // type
                    "%-4s  " +
                    // content age
                    "%s").c_str(),
                        backupIt->first.c_str(), 
                        approximate(backupIt->second.ds.getSize() + backupIt->second.ds.getSaved(), precisionLevel, statDetail > 2).c_str(),
                        approximate(backupIt->second.ds.getSize(), precisionLevel, statDetail > 2).c_str(), 
                        approximate(backupIt->second.dirs, precisionLevel, statDetail > 2).c_str(), 
                        approximate(backupIt->second.slinks, precisionLevel, statDetail > 2).c_str(), 
                        approximate(backupIt->second.modifiedFiles, precisionLevel, statDetail > 2).c_str(),
                        seconds2hms(backupIt->second.duration).c_str(),
                        timeDetail->tm_mon  == 0 && timeDetail->tm_mday == 1 ? "Year" : timeDetail->tm_mday == 1 ? "Mnth" : timeDetail->tm_wday == config.settings[sDOW].ivalue() ? "Week" : "Day",
                        backupIt->second.finishTime ? timeDiff(mktimeval(backupIt->second.finishTime)).c_str() : "?");

            cout << result << endl;
            ++backupIt;
            lastMonthYear = monthYear;
        }

        return true;
    }

    return false;
}


void _displayDetailedStats(BackupConfig& config, int statDetail) {
    int fnameLen = 0, numDay = 0, numWeek = 0, numMonth = 0, numYear = 0;
    size_t bytesUsed = 0;
    size_t bytesSaved = 0;
    set<ino_t> countedInode;
    set<string> dayUnique;

    if (_displayDetailedFaubStats(config, statDetail))
        return;

    // calcuclate stats from the entire list of backups
    for (auto &raw: config.cache.rawData) {

        // track the length of the longest filename for formatting
        fnameLen = (int)max(fnameLen, raw.second.filename.length());
        
        // calculate total bytes used and saved
        if (countedInode.find(raw.second.inode) == countedInode.end()) {
            countedInode.insert(raw.second.inode);
            bytesUsed += raw.second.size;
        }
        else
            bytesSaved += raw.second.size;

        auto timeString = to_string(raw.second.date_year) + to_string(raw.second.date_month) + to_string(raw.second.date_day);
        
        if (dayUnique.find(timeString) == dayUnique.end()) {
            dayUnique.insert(timeString);
            if (raw.second.date_month == 1 && raw.second.date_day == 1)
                ++numYear;
            else if (raw.second.date_day == 1)
                ++numMonth;
            else if (raw.second.dow == config.settings[sDOW].ivalue())
                ++numWeek;
            else
                ++numDay;
        }
    }

    // calcuclate percentage saved
    int saved = floor((1 - (long double)(bytesUsed / (bytesUsed + bytesSaved))) * 100 + 0.5);

    auto line = horizontalLine(60);
        
    // print top summary of backups
    unsigned long rawSize = config.cache.rawData.size();
    unsigned long md5Size = config.cache.indexByMD5.size();
    cout << line << "\n";
    
    if (config.settings[sTitle].value.length())
        cout << "Profile: " << config.settings[sTitle].value << "\n";
    
    cout << "Directory: " << config.settings[sDirectory].value << " (" << config.settings[sBackupFilename].value << ")\n";
    cout << rawSize << " backup" << s((int)rawSize) << ", " << md5Size << " unique" << s((int)md5Size) << "\n";
    cout << approximate(bytesUsed + bytesSaved) << " using " << approximate(bytesUsed) << " on disk (saved " << saved << "%)\n";
    cout << "Retention stats:\n";
    cout << "\t• " << numDay << " of " << config.settings[sDays].ivalue() << " daily\n";
    cout << "\t• " << numWeek << " of " << config.settings[sWeeks].ivalue() << " weekly\n";
    cout << "\t• " << numMonth << " of " << config.settings[sMonths].ivalue() << " monthly\n";
    cout << "\t• " << numYear << " of " << config.settings[sYears].ivalue() << " yearly\n";
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
                (raw_it->second.links == 1 || !raw_it->second.fnameDayAge);

            // format the detail for output
            char result[1000];
            snprintf(result, sizeof(result), 
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
                        raw_it->second.date_month == 1 && raw_it->second.date_day == 1 ? "Year" : raw_it->second.date_day == 1 ? "Mnth" : raw_it->second.dow == config.settings[sDOW].ivalue() ? "Week" : "Day",
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


void displayDetailedStatsWrapper(ConfigManager& configManager, int statDetail) {
    if (configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp)
        _displayDetailedStats(configManager.configs[configManager.activeConfig], statDetail);
    else {
        bool previous = false;
        for (auto &config: configManager.configs) {
            if (!config.temp) {

                if (previous)
                    cout << "\n\n";

                _displayDetailedStats(config, statDetail);
                previous = true;
            }
        }
    }
}

