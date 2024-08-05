
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
#include "FastCache.h"
#include "tagging.h"

#define NUMSTATDETAILS        10

using namespace std;
string oldMessage = ">24H old";


/* The summaryStats structure is used to return stat totals on a given config to the calling function.
 * Some data is broken out into individual totals (the longs).  Other is returned as strings for
 * easy display (stringOutput).
 */
struct summaryStats {
    bool inProcess;
    bool archived;
    size_t lastBackupBytes;
    time_t lastBackupTime;
    time_t firstBackupTime;
    size_t totalUsed;
    size_t totalSaved;
    long numberOfBackups;
    long uniqueBackups;
    unsigned long duration;
    string stringOutput[NUMSTATDETAILS] = {"", "(no backups found)", "-", "00:00:00", "0", "0", "0%"};
    
    summaryStats() {
        inProcess = archived = false;
        lastBackupBytes = firstBackupTime = lastBackupTime = totalUsed = totalSaved = numberOfBackups = uniqueBackups = duration = 0;
    }
};


summaryStats calculateSummaryStats(BackupConfig& config, int statDetail = 0) {
    set<ino_t> countedInode;
    struct summaryStats resultStats;
    int precisionLevel = statDetail > 3 ? 0 : statDetail > 1 ? 1 : -1;
    string processAge;
        
    // handle faub configs first
    if (config.isFaub()) {
        if (config.fcache.size() < 1) {
            resultStats.stringOutput[0] = config.settings[sTitle].value;
            resultStats.inProcess = config.fcache.getInProcessFilename().length() > 0;
            resultStats.archived = str2bool(config.settings[sArchive].value);
            return resultStats;
        }
        
        string inProcessFilename = config.fcache.getInProcessFilename();
        
#ifdef __APPLE__
        struct stat statBuf;
        if (inProcessFilename.length() && !mystat(inProcessFilename, &statBuf))
            processAge = seconds2hms(GLOBALS.startupTime - statBuf.st_birthtime);
#endif
        
        // set numeric stats
        auto ds = config.fcache.getTotalStats();
        resultStats.totalUsed = ds.getSize();
        resultStats.totalSaved = ds.getSaved();
        resultStats.numberOfBackups = resultStats.uniqueBackups = config.fcache.size();
        resultStats.lastBackupBytes = config.fcache.getLastBackup()->second.ds.getSize() + config.fcache.getLastBackup()->second.ds.getSaved();
        resultStats.lastBackupTime = config.fcache.getLastBackup()->second.finishTime;
        resultStats.firstBackupTime = config.fcache.getFirstBackup()->second.finishTime;
        resultStats.duration = config.fcache.getLastBackup()->second.duration;
        resultStats.inProcess = inProcessFilename.length() > 0;
        resultStats.archived = str2bool(config.settings[sArchive].value);
                    
        // set string stats
        auto t = localtime(&resultStats.lastBackupTime);
        char fileTime[20];
        strftime(fileTime, sizeof(fileTime), "%X", t);
        
        int saved = floor((1 - (long double)resultStats.totalUsed / ((long double)resultStats.totalUsed + (long double)resultStats.totalSaved)) * 100 + 0.5);
        string lastBackupFilename = pathSplit(config.fcache.getLastBackup()->first).file;
        
        string soutput[NUMSTATDETAILS] = {
            config.settings[sTitle].value,
            lastBackupFilename,
            fileTime,
            seconds2hms(resultStats.duration),
            (approximate(resultStats.lastBackupBytes, precisionLevel, statDetail == 3 || statDetail == 5) +
             " (" + approximate(resultStats.totalUsed, precisionLevel, statDetail == 3 || statDetail == 5) + ")"),
            to_string(resultStats.uniqueBackups),
            to_string(saved) + "%",
            config.fcache.getFirstBackup() != config.fcache.getEnd() ? timeDiff(mktimeval(config.fcache.getFirstBackup()->second.finishTime)) : "",
            config.fcache.getLastBackup() != config.fcache.getEnd() ? timeDiff(mktimeval(config.fcache.getLastBackup()->second.finishTime)) : "",
            processAge.length() ? processAge : GLOBALS.startupTime - config.fcache.getLastBackup()->second.finishTime > 2*60*60*24 ? oldMessage : ""
        };
                
        for (int i = 0; i < NUMSTATDETAILS; ++i)
            resultStats.stringOutput[i] = soutput[i];
        
        return resultStats;
    }
    
    // then regular configs
    resultStats.numberOfBackups = config.cache.rawData.size();
    if (resultStats.numberOfBackups < 1) {
        resultStats.stringOutput[0] = config.settings[sTitle].value;
        resultStats.inProcess = false;
        resultStats.archived = str2bool(config.settings[sArchive].value);
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
        if (config.cache.inProcess.length() && !mystat(config.cache.inProcess, &statBuf))
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
            (approximate(last_it->second.size, precisionLevel, statDetail == 3 || statDetail == 5) + " (" + approximate(resultStats.totalUsed, precisionLevel, statDetail == 3 || statDetail == 5) + ")"),
            (to_string(resultStats.uniqueBackups) + " (" + to_string(resultStats.numberOfBackups) + ")"),
            to_string(saved) + "%",
            first_it->second.mtime ? timeDiff(mktimeval(first_it->second.mtime)) : first_it->second.name_mtime ? timeDiff(mktimeval(first_it->second.name_mtime)) : "?",
            last_it->second.mtime ? timeDiff(mktimeval(last_it->second.mtime)) : "?",
            processAge.length() ? processAge : GLOBALS.startupTime - last_it->second.name_mtime > 2*60*60*24 ? oldMessage : ""
        };
        
        for (int i = 0; i < NUMSTATDETAILS; ++i)
            resultStats.stringOutput[i] = soutput[i];
        
        resultStats.inProcess = config.cache.inProcess.length();
        resultStats.duration = last_it->second.duration;
        resultStats.lastBackupBytes = last_it->second.size;
        resultStats.lastBackupTime = last_it->second.mtime;
        resultStats.firstBackupTime = first_it->second.mtime;
        resultStats.archived = str2bool(config.settings[sArchive].value);
    }
    
    return resultStats;
}


void displaySummaryStatsWrapper(ConfigManager& configManager, int statDetail, bool cacheOnly) {
    struct profileStatsType {
        bool inProcess;
        bool archived;
        time_t firstBackupTime;
        time_t lastBackupTime;
        profileStatsType(bool p, bool a, time_t f, time_t l) { inProcess = p; archived = a; firstBackupTime = f; lastBackupTime = l; }
    };
    
    FastCache fastCache;
    int nonTempConfigs = 0;
    int precisionLevel = statDetail > 3 ? 0 : statDetail > 1 ? 1 : -1;
    struct summaryStats perStats;
    struct summaryStats totalStats;
    vector<string> statStrings;
    vector<profileStatsType> profileStats;
    bool singleConfig = configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp && !cacheOnly;
    
    // calculate totals
    if (singleConfig) {
        perStats = calculateSummaryStats(configManager.configs[configManager.activeConfig], statDetail);
        profileStats.insert(profileStats.end(), profileStatsType(perStats.inProcess, perStats.archived, perStats.firstBackupTime, perStats.lastBackupTime));
        
        for (int i = 0; i < NUMSTATDETAILS; ++i)
            statStrings.insert(statStrings.end(), perStats.stringOutput[i]);
    }
    else {
        for (auto &config: configManager.configs) {
            if (!config.temp && !config.settings[sPaths].value.length()) {
                ++nonTempConfigs;
                perStats = calculateSummaryStats(config, statDetail);
                totalStats.lastBackupBytes += perStats.lastBackupBytes;
                totalStats.totalUsed += perStats.totalUsed;
                totalStats.totalSaved += perStats.totalSaved;
                totalStats.numberOfBackups += perStats.numberOfBackups;
                totalStats.uniqueBackups += perStats.uniqueBackups;
                totalStats.duration += perStats.duration;
                
                profileStats.insert(profileStats.end(), profileStatsType(perStats.inProcess, perStats.archived, perStats.firstBackupTime, perStats.lastBackupTime));
                
                if (config.fcache.size())
                    fastCache.appendFile(pathSplit(config.fcache.getLastBackup()->first).dir);
                else {
                    auto bIt = config.cache.getLastBackup();
                    if (bIt != config.cache.getEnd())
                        fastCache.appendFile(pathSplit(bIt->second.filename).dir);
                }
                
                for (int i = 0; i < NUMSTATDETAILS; ++i)
                    statStrings.insert(statStrings.end(), perStats.stringOutput[i]);
            }
        }
    }
    
    // add totals into totalStats
    if (!singleConfig) {
        statStrings.insert(statStrings.end(), "");   // skip profile
        statStrings.insert(statStrings.end(), "");   // skip most recent backup
        statStrings.insert(statStrings.end(), "");   // skip finish time
        statStrings.insert(statStrings.end(), seconds2hms(totalStats.duration));
        statStrings.insert(statStrings.end(), approximate(totalStats.lastBackupBytes, precisionLevel, statDetail == 3 || statDetail == 5) + " (" +
                           approximate(totalStats.totalUsed, precisionLevel, statDetail == 3 || statDetail == 5) + ")");
        statStrings.insert(statStrings.end(), to_string(totalStats.uniqueBackups) + " (" + to_string(totalStats.numberOfBackups) + ")");
        statStrings.insert(statStrings.end(), to_string(int(floor((1 - ((long double)totalStats.totalUsed / ((long double)totalStats.totalUsed + (long double)totalStats.totalSaved))) * 100 + 0.5))) + "%");
        statStrings.insert(statStrings.end(), totalStats.totalSaved ? string(string("Saved ") + approximate(totalStats.totalSaved, precisionLevel, statDetail == 3 || statDetail == 5) + " from "
                                                                             + approximate(totalStats.totalUsed + totalStats.totalSaved, precisionLevel, statDetail == 3 || statDetail == 5)) : "");
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
        
    if (numberStatStrings >= NUMSTATDETAILS) {
        // print the header row
        // the blank at the end isn't just for termination; it's used for "in process" status
        string headerText = BOLDBLUE;
        string headers[] = { "Profile", "Most Recent Backup", "Finish@", "Duration", "Size (Total)", "Uniq (T)", "Saved", "Age Range", "" };
        
        int x = -1;
        while (headers[++x].length()) {
            headerText += (x == 0 ? "" : "  ") + headers[x];
            
            // pad the headers to line up with the longest item in each column
            if (colLen[x] > headers[x].length())
                headerText += string(colLen[x] - headers[x].length(), ' ');
        }
        
        !cacheOnly && cout << headerText << RESET << "\n";
        fastCache.appendStatus(FASTCACHETYPE (headerText + RESET, 0, 1));
        
        // setup line formatting
        string lineFormat;
        for (int x = 0; x < NUMSTATDETAILS - 3; ++x)
            if (max(headers[x].length(), colLen[x]))
                lineFormat += (lineFormat.length() ? "  " : "") + string("%") + string(x == 6 ? "" : "-") +
                to_string(max(headers[x].length(), colLen[x])) + "s";   // 6th column is right-justified
        lineFormat += "  ";
                
        // print line by line results
        char result[1000];
        int line = 0;
        while (line * NUMSTATDETAILS < numberStatStrings - (singleConfig ? 0 : NUMSTATDETAILS)) {
            string HIGHLIGHT = profileStats[line].inProcess ? BOLDGREEN : profileStats[line].archived ? BLUE : BOLDBLUE;
            string BRACKETO = profileStats[line].inProcess ? "{" : "[";
            string BRACKETC = profileStats[line].inProcess ? "}" : "]";
            string msg = statStrings[line * NUMSTATDETAILS + 9];
            bool is_old = msg == oldMessage;
            bool gotAge = statStrings[line * NUMSTATDETAILS + 7].length() || statStrings[line * NUMSTATDETAILS + 8].length();
            
            snprintf(result, sizeof(result), lineFormat.c_str(),
                     statStrings[line * NUMSTATDETAILS].c_str(),
                     statStrings[line * NUMSTATDETAILS + 1].c_str(),
                     statStrings[line * NUMSTATDETAILS + 2].c_str(),
                     (profileStats[line].archived ? "ARCHIVED" : statStrings[line * NUMSTATDETAILS + 3]).c_str(),
                     statStrings[line * NUMSTATDETAILS + 4].c_str(),
                     statStrings[line * NUMSTATDETAILS + 5].c_str(),
                     statStrings[line * NUMSTATDETAILS + 6].c_str());
            
            string ageDetail = string(HIGHLIGHT) + BRACKETO + RESET +
                (gotAge ? statStrings[line * NUMSTATDETAILS + 7] + HIGHLIGHT +
                 string(" -> ") + RESET + statStrings[line * NUMSTATDETAILS + 8] : "-") +
                HIGHLIGHT + BRACKETC + " " + (is_old ? string(BOLDRED) + msg : msg);
            
            fastCache.appendStatus(FASTCACHETYPE (result, profileStats[line].firstBackupTime, profileStats[line].lastBackupTime));
            !cacheOnly && cout << result << ageDetail << RESET << "\n";
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
            !cacheOnly && cout << BOLDWHITE << "TOTALS" << spaces << result << RESET << "\n";
            fastCache.appendStatus(FASTCACHETYPE (string(BOLDWHITE) + "TOTALS" + spaces + result + RESET, 0, 1));
        }

        fastCache.commit();
    }
    else
        !cacheOnly && cout << "no backups found." << endl;
     
    return;
}


bool _displayDetailedFaubStats(BackupConfig& config, int statDetail, Tagging tags) {
    int precisionLevel = statDetail > 3 ? 0 : statDetail > 1 ? 1 : -1;
    
    if (config.isFaub() && config.fcache.size()) {
        auto line = horizontalLine(60);
        auto bkups = config.fcache.getNumberOfBackups();
        auto stats = config.fcache.getTotalStats();
        int saved = floor((1 - (long double)stats.getSize() / (stats.getSize() + stats.getSaved())) * 100 + 0.5);
        
        vector<headerType> headers{ {"Date"}, {"Size"}, {"Used"}, {"Dirs"}, {"SymLks"}, {"Mods"}, {"Duration", 8}, {"Type", 4}, {"Age"}, {"Tags"}};
        bool summaryShown = false;
        string introSummary = line + "\n";
        if (config.settings[sTitle].value.length())
            introSummary += "Profile: " + config.settings[sTitle].value + "\n";
        introSummary += "Directory: " + config.settings[sDirectory].value + "\n";
        introSummary += plural((int)bkups, "backup") + "\n";
        introSummary += approximate(stats.getSize() + stats.getSaved(), precisionLevel, statDetail == 3 || statDetail == 5) + " using "
        + approximate(stats.getSize(), precisionLevel, statDetail == 3 || statDetail == 5) + " on disk (saved " + to_string(saved) + "%)\n";
        
        auto [target, average, detail] = config.getBloatTarget();
        introSummary += approximate(average) + " per backup average used\n";
        
        if (config.fcache.getInProcessFilename().length())
            introSummary += YELLOW + config.fcache.getInProcessFilename() + RESET + " (in process)\n";
        
        // determine the backup max filename length and rention match counts
        set<string> dayUnique;
        struct tm *timeDetail;
        int numDay = 0, numWeek = 0, numMonth = 0, numYear = 0;
        
        vector<string> ages;
        auto backupIt = config.fcache.getFirstBackup();
        while (backupIt != config.fcache.getEnd()) {
            if (!GLOBALS.cli.count(CLI_TAG) || tags.match(GLOBALS.cli[CLI_TAG].as<string>(), backupIt->first)) {
                ages.insert(ages.end(), backupIt->second.finishTime ? timeDiff(mktimeval(backupIt->second.finishTime)).c_str() : "?");
                
                headers[0].setMaxLength(backupIt->first.length());
                headers[1].setMaxLength(approximate(backupIt->second.ds.getSize() + backupIt->second.ds.getSaved(), precisionLevel, statDetail == 3 || statDetail == 5).length());
                headers[2].setMaxLength(approximate(backupIt->second.ds.getSize(), precisionLevel, statDetail == 3 || statDetail == 5).length());
                headers[3].setMaxLength(approximate(backupIt->second.dirs, precisionLevel, statDetail == 3 || statDetail == 5).length());
                headers[4].setMaxLength(approximate(backupIt->second.slinks, precisionLevel, statDetail == 3 || statDetail == 5).length());
                headers[5].setMaxLength(approximate(backupIt->second.modifiedFiles, precisionLevel, statDetail == 3 || statDetail == 5).length());
                headers[8].setMaxLength(ages.back().length());
                
                string itemTags = perlJoin(", ", tags.tagsOnBackup(backupIt->first));
                headers[9].setMaxLength(itemTags.length());
                if (itemTags.length())
                    headers[9].override = true;

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
            }
            ++backupIt;
        }
        
        introSummary +=  "Retention stats:\n";
        introSummary += "\t• " + to_string(numDay) + " of " + to_string(config.settings[sDays].ivalue()) + " daily\n";
        introSummary += "\t• " + to_string(numWeek) + " of " + to_string(config.settings[sWeeks].ivalue()) + " weekly\n";
        introSummary += "\t• " +  to_string(numMonth) + " of " + to_string(config.settings[sMonths].ivalue()) + " monthly\n";
        introSummary += "\t• " + to_string(numYear) + " of " + to_string(config.settings[sYears].ivalue()) + " yearly\n";
        introSummary += line;
        
        string lastMonthYear;
        char result[3000];
        auto ageIt = ages.begin();
        backupIt = config.fcache.getFirstBackup();
        while (backupIt != config.fcache.getEnd())  {
            if (!GLOBALS.cli.count(CLI_TAG) || tags.match(GLOBALS.cli[CLI_TAG].as<string>(), backupIt->first)) {
                
                if (!summaryShown) {
                    summaryShown = true;
                    cout << introSummary << endl;
                }
                
                timeDetail = localtime(&backupIt->second.finishTime);
                string monthYear = backupIt->second.finishTime ? vars2MY(timeDetail->tm_mon+1, timeDetail->tm_year+1900) : "Unknown";
                
                // print the month header
                if (lastMonthYear != monthYear) {
                    cout << BOLDBLUE << "\n";
                    
                    for (auto &header: headers) {
                        string shownHeader = header.name == "Date" ? monthYear : header.name;
                        
                        if (header.name != "Tags" || header.override) {
                            
                            // first column, no leading space
                            if (header.name != "Date")
                                cout << "  ";
                            
                            cout << shownHeader;
                            
                            // padding spaces to handle field lengths
                            if (header.maxLength > shownHeader.length()) {
                                string spaces(header.maxLength - shownHeader.length(), ' ');
                                cout << spaces;
                            }
                        }
                    }
                    
                    cout << RESET << "\n";
                }
                
                snprintf(result, sizeof(result),
                         // filename
                         string(string("%-") + to_string(headers[0].maxLength) + "s  " +
                                // size
                                "%" + to_string(headers[1].maxLength) + "s  " +
                                // used
                                "%" + to_string(headers[2].maxLength) + "s  " +
                                // dirs
                                "%" + to_string(headers[3].maxLength) + "s  " +
                                // symlinks
                                "%" + to_string(headers[4].maxLength) + "s  " +
                                // modifies
                                "%" + to_string(headers[5].maxLength) + "s  " +
                                // duration
                                "%s  " +
                                // type
                                "%-4s  " +
                                // content age
                                "%-" + to_string(headers[8].maxLength) + "s  " +
                                // tags
                                "%s").c_str(),
                         backupIt->first.c_str(),
                         approximate(backupIt->second.ds.getSize() + backupIt->second.ds.getSaved(), precisionLevel, statDetail == 3 || statDetail == 5).c_str(),
                         approximate(backupIt->second.ds.getSize(), precisionLevel, statDetail == 3 || statDetail == 5).c_str(),
                         approximate(backupIt->second.dirs, precisionLevel, statDetail == 3 || statDetail == 5).c_str(),
                         approximate(backupIt->second.slinks, precisionLevel, statDetail == 3 || statDetail == 5).c_str(),
                         approximate(backupIt->second.modifiedFiles, precisionLevel, statDetail == 3 || statDetail == 5).c_str(),
                         seconds2hms(backupIt->second.duration).c_str(),
                         timeDetail->tm_mon  == 0 && timeDetail->tm_mday == 1 ? "Year" : timeDetail->tm_mday == 1 ? "Mnth" : timeDetail->tm_wday == config.settings[sDOW].ivalue() ? "Week" : "Day",
                         //backupIt->second.finishTime ? timeDiff(mktimeval(backupIt->second.finishTime)).c_str() : "?",
                         ageIt++->c_str(),
                         perlJoin(", ", tags.tagsOnBackup(backupIt->first)).c_str());
                
                cout << result << endl;
                lastMonthYear = monthYear;
            }
            ++backupIt;
        }
        
        return true;
    }
    
    return false;
}


void _displayDetailedStats(BackupConfig& config, int statDetail, Tagging &tags) {
    int numDay = 0, numWeek = 0, numMonth = 0, numYear = 0;
    size_t bytesUsed = 0;
    size_t bytesSaved = 0;
    set<ino_t> countedInode;
    set<string> dayUnique;
    int precisionLevel = statDetail > 3 ? 0 : statDetail > 1 ? 1 : -1;
    //string headers[] = { "x", "Size", "Duration", "Type", "Lnks", "Age", "" };
    vector<headerType> headers { {"Date"}, {"Size"}, {"Duration", 8}, {"Type", 4}, {"Lnks"}, {"Age"} };
    
    if (_displayDetailedFaubStats(config, statDetail, tags) || GLOBALS.cli.count(CLI_TAG))
        return;
        
    // calcuclate stats from the entire list of backups
    for (auto &raw: config.cache.rawData) {
        
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
        
        headers[0].setMaxLength(raw.second.filename.length());
        headers[1].setMaxLength(approximate(raw.second.size, precisionLevel, statDetail == 3 || statDetail == 5).length());
        headers[4].setMaxLength(approximate(raw.second.links, precisionLevel, statDetail == 3 || statDetail == 5).length());
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
    cout << plural((int)rawSize, "backup") << ", " << plural((int)md5Size, "unique") << "\n";
    cout << approximate(bytesUsed + bytesSaved, precisionLevel, statDetail == 3 || statDetail == 5) << " using " << approximate(bytesUsed, precisionLevel, statDetail == 3 || statDetail == 5) << " on disk (saved " << saved << "%)\n";
    
    if (config.cache.inProcess.length())
        cout << YELLOW << config.fcache.getInProcessFilename() << RESET << " (in process)" << endl;
    
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
            string monthYear = raw_it->second.date_month > 0 && raw_it->second.date_month < 13 && raw_it->second.date_year < 3000 && raw_it->second.date_year > 0
            ? vars2MY(raw_it->second.date_month, raw_it->second.date_year) : "[Unknown]";
            
            // print the month header
            if (lastMonthYear != monthYear) {
                cout << BOLDBLUE << "\n";
                
                for (auto &header: headers) {
                    string shownHeader = header.name == "Date" ? monthYear : header.name;
                    
                    if (header.name != "Tags" || header.override) {
                        
                        // first column, no leading space
                        if (header.name != "Date")
                            cout << "  ";
                        
                        cout << shownHeader;
                        
                        // padding spaces to handle field lengths
                        if (header.maxLength > shownHeader.length()) {
                            string spaces(header.maxLength - shownHeader.length(), ' ');
                            cout << spaces;
                        }
                    }
                }
                
                cout << RESET << "\n";
            }
            
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
                     string(string("%-") + to_string(headers[0].maxLength) + "s  " +
                            // size
                            "%" + to_string(headers[1].maxLength) + "s  " +
                            // duration
                            "%s  " +
                            // type
                            "%-" + to_string(headers[3].maxLength) + "s  " +
                            // links
                            "%" + to_string(headers[4].maxLength) + "u  " +
                            // content age
                            "%s").c_str(),
                     raw_it->second.filename.c_str(), approximate(raw_it->second.size, precisionLevel, statDetail == 3 || statDetail == 5).c_str(),
                     seconds2hms(raw_it->second.duration).c_str(),
                     raw_it->second.date_month == 1 && raw_it->second.date_day == 1 ? "Year" : raw_it->second.date_day == 1 ? "Mnth" : raw_it->second.dow == config.settings[sDOW].ivalue() ? "Week" : "Day",
                     raw_it->second.links, raw_it->second.mtime ? timeDiff(mktimeval(prectime ? raw_it->second.mtime : raw_it->second.name_mtime)).c_str() : "?");
            
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
    Tagging tags;
    
    if (configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp)
        _displayDetailedStats(configManager.configs[configManager.activeConfig], statDetail, tags);
    else {
        bool previous = false;
        for (auto &config: configManager.configs) {
            if (!config.temp && !config.settings[sPaths].value.length()) {
                
                if (previous && !GLOBALS.cli.count(CLI_TAG))
                    cout << "\n\n";
                
                _displayDetailedStats(config, statDetail, tags);
                previous = true;
            }
        }
    }
}

