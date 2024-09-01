
#include <set>
#include <vector>
#include <algorithm>
#include <libgen.h>
#include <math.h>
#include <sys/stat.h>
#include <assert.h>

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


void produceSummaryStatsWrapper(ConfigManager& configManager, int statDetail, bool cacheOnly) {
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
    
    tableManager headers { {"Profile"}, {"Most Recent Backup"}, {"Finish@"}, {"Duration"}, {"Size (Total)"}, {"Uniq (T)"}, {"Saved"}, {"Age Range"} };
    
    // determine the longest length entry of each column to allow consistent horizontal formatting
    int numberStatStrings = (int)statStrings.size();
    for (int column = 0; column < NUMSTATDETAILS - 1; ++column) {
        int line = 0;
        
        while (NUMSTATDETAILS * line + column < numberStatStrings) {
            if (column == 7)  // cols 7 and 8 get combined
                headers[column].setMax(statStrings[NUMSTATDETAILS * line + column].length() + statStrings[NUMSTATDETAILS * line + column+1].length() + 6);
            else if (column > 7)
                headers[column].setMax(statStrings[NUMSTATDETAILS * line + column+1].length());
            else
                headers[column].setMax(statStrings[NUMSTATDETAILS * line + column].length());
            
            ++line;
        }
    }
    
    // print the header row
    if (numberStatStrings >= NUMSTATDETAILS) {
        string headerText;
        
        headerText = headers.displayHeader("", true);
        !cacheOnly && NOTQUIET && cout << headerText;
        fastCache.appendStatus(FASTCACHETYPE (headerText + RESET, 0, 1));
        
        // setup line formatting
        string lineFormat;
        for (int x = 0; x < NUMSTATDETAILS - 3; ++x)
            lineFormat += (lineFormat.length() ? "  " : "") + string("%") + string(x == 6 ? "" : "-") +
            to_string(headers[x].maxLength) + "s";   // 6th column is right-justified
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
                                                    "%-" + to_string(headers[3].maxLength) + "s  " +
                                                    "%-" + to_string(headers[4].maxLength) + "s  " +
                                                    "%-" + to_string(headers[5].maxLength) + "s  " +
                                                    "%"  + to_string(headers[6].maxLength) + "s  " +
                                                    "%-" + to_string(headers[7].maxLength) + "s").c_str(),
                     statStrings[line * NUMSTATDETAILS + 3].c_str(),
                     statStrings[line * NUMSTATDETAILS + 4].c_str(),
                     statStrings[line * NUMSTATDETAILS + 5].c_str(),
                     statStrings[line * NUMSTATDETAILS + 6].c_str(),
                     statStrings[line * NUMSTATDETAILS + 7].c_str(),
                     statStrings[line * NUMSTATDETAILS + 8].c_str());
            
            string spaces(headers[0].maxLength + headers[1].maxLength + headers[2].maxLength, ' ');
            !cacheOnly && cout << BOLDWHITE << "TOTALS" << spaces << result << RESET << "\n";
            fastCache.appendStatus(FASTCACHETYPE (string(BOLDWHITE) + "TOTALS" + spaces + result + RESET, 0, 1));
        }
        
        fastCache.commit();
    }
    else
        !cacheOnly && cout << "no backups found." << endl;
    
    return;
}


void displaySingleLineDetails(map<unsigned int, BackupEntry>::iterator rawIt, BackupCache& cache, tableManager& table, colorRotator& color, string lastMD5, int dow, int precision, int statDetail) {
    // file age can be calculated from the mtime which is an accurate number returned by
    // stat(), but in the case of multiple backups hardlinked together (due to identical content)
    // will actually be the mtime of the most recent of those files
    //      OR
    // file age can be calculated from name_mtime, which is based on the date in the filename rounded
    // to midnight.
    //
    // precisetime (prectime) says use the filesystem mtime if the number of hard links is 1 (i.e.
    // only one backup using that inode and mtime) or if the file is less than a day old.  then we
    // get precision.  if more than one file shares that inode (links > 1) and the file is older than
    // today, revert to the midnight rounded date derived from the filename.
    //
    // if the mtime and the name_mtime refer to completely different days then go non-precise and use
    // the name_mtime.
    bool prectime = mtimesAreSameDay(rawIt->second.mtime, rawIt->second.name_mtime) && (rawIt->second.links == 1 || !rawIt->second.fnameDayAge);
    bool commas = statDetail == 3 || statDetail == 5;
    
    table.addRowData(rawIt->second.filename);
    table.addRowData(approximate(rawIt->second.size, precision, commas));
    table.addRowData(seconds2hms(rawIt->second.duration));
    table.addRowData(rawIt->second.date_month == 1 && rawIt->second.date_day == 1 ? "Year" : rawIt->second.date_day == 1 ? "Mnth" : rawIt->second.dow == dow ? "Week" : "Day");
    table.addRowData(to_string(rawIt->second.links));
    table.addRowData(rawIt->second.mtime ? timeDiff(mktimeval(prectime ? rawIt->second.mtime : rawIt->second.name_mtime)) : "?");
    
    // if there's more than 1 file with this MD5 then color code it as a set; otherwise no color
    if (cache.getByMD5(rawIt->second.md5).size() > 1) {
        
        // rotate the color if we're on a new set (i.e. new md5)
        if (lastMD5.length() && lastMD5 != rawIt->second.md5)
            ++color;
        
        cout << color.current();
    }
        
    cout << table.displayRow() << RESET << "\n";
}


void analyseSingleBackupLineDetails(tableManager& table, detailType& detail, set<ino_t>& countedInode, BackupEntry& raw, int dow, int precision, int statDetail) {
    bool commas = statDetail == 3 || statDetail == 5;
    
    if (countedInode.find(raw.inode) == countedInode.end()) {
        countedInode.insert(raw.inode);
        detail.bytesUsed += raw.size;
    }
    else
        detail.bytesSaved += raw.size;
    
    auto timeString = to_string(raw.date_year) + to_string(raw.date_month) + to_string(raw.date_day);
    
    if (detail.dayUnique.find(timeString) == detail.dayUnique.end()) {
        detail.dayUnique.insert(timeString);
        if (raw.date_month == 1 && raw.date_day == 1)
            ++detail.yearly;
        else if (raw.date_day == 1)
            ++detail.monthly;
        else if (raw.dow == dow)
            ++detail.weekly;
        else
            ++detail.daily;
    }
    
    table[0].setMax(raw.filename.length());
    table[1].setMax(approximate(raw.size, precision, commas).length());
    table[4].setMax(approximate(raw.links, precision, commas).length());
}


void analyseFaubLineDetails(tableManager& table, detailType& detail, myMapIT& backupIt, Tagging& tags, int dow, int precision, int statDetail) {
    bool commas = statDetail == 3 || statDetail == 5;

    /* here we pre-calculate (in this 'store' routine) age for faub configs because we need to know the max column length age will require in order to line
       up the fields that come to the right of age (hold, tag).  because singlefile backups don't currently support holds or tags, age isn't calculated in
       that 'store' function, but instead at the time of display. */
    
    detail.ages.insert(detail.ages.end(), backupIt->second.finishTime ? timeDiff(mktimeval(backupIt->second.finishTime)).c_str() : "?");

    table[0].setMax(backupIt->first.length());
    table[1].setMax(approximate(backupIt->second.ds.getSize() + backupIt->second.ds.getSaved(), precision, commas).length());
    table[2].setMax(approximate(backupIt->second.ds.getSize(), precision, commas).length());
    table[3].setMax(approximate(backupIt->second.dirs, precision, commas).length());
    table[4].setMax(approximate(backupIt->second.slinks, precision, commas).length());
    table[5].setMax(approximate(backupIt->second.modifiedFiles, precision, commas).length());
    table[8].setMax(detail.ages.back().length());
    table[9].setMax(backupIt->second.holdDate ? 24 : 0);
    table[10].setMax(perlJoin(", ", tags.tagsOnBackup(backupIt->first)).length());

    auto timeDetail = localtime(&backupIt->second.finishTime);
    auto timeString = to_string(timeDetail->tm_year) + to_string(timeDetail->tm_mon) + to_string(timeDetail->tm_mday);

    if (detail.dayUnique.find(timeString) == detail.dayUnique.end()) {
        detail.dayUnique.insert(timeString);
        
        if (timeDetail->tm_mon == 0 && timeDetail->tm_mday == 1)
            detail.yearly++;
        else if (timeDetail->tm_mday == 1)
            detail.monthly++;
        else if (timeDetail->tm_wday == dow)
            detail.weekly++;
        else
            detail.daily++;
    }
}


void displaySectionIntro(BackupConfig& config, detailType& detail, int precision, int statDetail) {
    auto line = horizontalLine(60);
    auto bkups = config.isFaub() ? config.fcache.getNumberOfBackups() : config.cache.rawData.size();
    auto unique = config.isFaub() ? 0 : config.cache.indexByMD5.size();
    auto stats = config.fcache.getTotalStats();
    int saved = floor((1 - (long double)stats.getSize() / (stats.getSize() + stats.getSaved())) * 100 + 0.5);
    
    string introSummary = line + "\n";
    if (config.settings[sTitle].value.length())
        introSummary += "Profile: " + config.settings[sTitle].value + "\n";
    introSummary += "Directory: " + config.settings[sDirectory].value + "\n";
    
    if (bkups) {
        introSummary += plural((int)bkups, "backup") + (!config.isFaub() ? ", " + plural((int)unique, "unique") : "") + "\n";
        if (config.isFaub())
            introSummary += approximate(stats.getSize() + stats.getSaved(), precision, statDetail == 3 || statDetail == 5) + " using "
            + approximate(stats.getSize(), precision, statDetail == 3 || statDetail == 5) + " on disk (saved " + to_string(saved) + "%)\n";
        else
            introSummary += approximate(detail.bytesUsed + detail.bytesSaved, precision, statDetail == 3 || statDetail == 5) + string(" using ") + approximate(detail.bytesUsed, precision, statDetail == 3 || statDetail == 5) + " on disk (saved " + to_string(saved) + "%)\n";
        
        auto [bloatTarget, bloatAverage, bloatDetail] = config.getBloatTarget();
        introSummary += approximate(bloatAverage) + " per backup average used\n";
        
        if (config.fcache.getInProcessFilename().length())
            introSummary += YELLOW + config.fcache.getInProcessFilename() + RESET + " (in process)\n";
        
        introSummary +=  "Retention stats:\n";
        introSummary += "\t• " + to_string(detail.daily) + " of " + to_string(config.settings[sDays].ivalue()) + " daily\n";
        introSummary += "\t• " + to_string(detail.weekly) + " of " + to_string(config.settings[sWeeks].ivalue()) + " weekly\n";
        introSummary += "\t• " +  to_string(detail.monthly) + " of " + to_string(config.settings[sMonths].ivalue()) + " monthly\n";
        introSummary += "\t• " + to_string(detail.yearly) + " of " + to_string(config.settings[sYears].ivalue()) + " yearly\n";
        introSummary += line;
    }
    else
        introSummary += line + "\nNo backups found.";
    
    cout << introSummary << endl;
}


void displayFaubLineDetails(tableManager& table, myMapIT& backupIt, Tagging& tags, int dow, int precision, int statDetail, tm* timeDetail, string age) {
    bool commas = statDetail == 3 || statDetail == 5;
    
    table.addRowData(backupIt->first);
    table.addRowData(approximate(backupIt->second.ds.getSize() + backupIt->second.ds.getSaved(), precision, commas));
    table.addRowData(approximate(backupIt->second.ds.getSize(), precision, commas));
    table.addRowData(approximate(backupIt->second.dirs, precision, commas));
    table.addRowData(approximate(backupIt->second.slinks, precision, commas));
    table.addRowData(approximate(backupIt->second.modifiedFiles, precision, commas));
    table.addRowData(seconds2hms(backupIt->second.duration));
    table.addRowData(timeDetail->tm_mon == 0 && timeDetail->tm_mday == 1 ? "Year" : timeDetail->tm_mday == 1 ? "Mnth" : timeDetail->tm_wday == dow ? "Week" : "Day");
    table.addRowData(age);
    table.addRowData(backupIt->second.holdDate > 1 ? timeString(backupIt->second.holdDate) : backupIt->second.holdDate == 1 ? "Permanent" : "");
    table.addRowData(perlJoin(", ", tags.tagsOnBackup(backupIt->first)));
    
    cout << table.displayRow() << "\n";
}


bool shouldDisplayConfig(BackupConfig& config, ConfigManager& configManager) {
    bool oneConfigSelected = configManager.activeConfig > -1 && !configManager.configs[configManager.activeConfig].temp;;
    bool thisConfigSelected = !config.temp && config == configManager.configs[configManager.activeConfig];
    bool pathConfig = config.settings[sPaths].value.length();
    bool tempConfig = config.temp;
    
    if (pathConfig && oneConfigSelected && thisConfigSelected) {
        SCREENERR("--" << CLI_PATHS << " configs are client only and don't have associated backups.");
        exit(1);
    }
    
    if ((oneConfigSelected && !thisConfigSelected) || pathConfig || tempConfig)
        return false;
    
    return true;
}


void produceDetailedStats(ConfigManager& configManager, int statDetail) {
    tableManager singleFileTable { {"Date", 0, 1}, {"Size"}, {"Duration", 8}, {"Type", 4, 1}, {"Lnks"}, {"Age", 0, 1} };
    tableManager faubTable { {"Date", 0, 1}, {"Size"}, {"Used"}, {"Dirs"}, {"SymLks"}, {"Mods"}, {"Duration", 8}, {"Type", 4, 1}, {"Age", 0, 1}, {"Hold", 0, 1}, {"Tags"} };
    
    vector<string> colors { { GREEN, MAGENTA, CYAN, BLUE, YELLOW, BOLDGREEN, BOLDMAGENTA, BOLDYELLOW, BOLDCYAN } };
    vector<detailType> details;
    int precisionLevel = statDetail > 3 ? 0 : statDetail > 1 ? 1 : -1;
    Tagging tags;
    
    /* we need TWO walks through the configs.  one to determine the longest value in each field; that allows
     consistent column formating.  and a second to actually print the output with appropriate column padding. */
    
    // FIRST loop - calculate max lengths
    for (auto &config: configManager.configs) {
        
        if (!shouldDisplayConfig(config, configManager))
            continue;
        
        /* faub configs */
        if (config.isFaub() && !config.fcache.empty()) {
            auto backupIt = config.fcache.getFirstBackup();
            detailType detail;

            /* walk through faub backups */
            while (backupIt != config.fcache.getEnd()) {
                
                // no tag is specified (i.e. everything is valid) or the specified tag matches
                if (!GLOBALS.cli.count(CLI_TAG) || tags.match(GLOBALS.cli[CLI_TAG].as<string>(), backupIt->first)) {
                    analyseFaubLineDetails(faubTable, detail, backupIt, tags, config.settings[sDOW].ivalue(), precisionLevel, statDetail);
                }
                ++backupIt;
            }
            
            details.insert(details.end(), detail);
        }

        /* single-file backup configs */
        else {
            set<ino_t> countedInode;
            detailType detail;

            for (auto &raw: config.cache.rawData) {
                analyseSingleBackupLineDetails(singleFileTable, detail, countedInode, raw.second, config.settings[sDOW].ivalue(), precisionLevel, statDetail);
            }
            
            details.insert(details.end(), detail);
        }
    }
    
    typedef vector<BackupConfig>::iterator bconfigIt;
    typedef vector<detailType>::iterator detailIt;
    string lastMonthYear;

    // SECOND loop - display output
    for (pair<bconfigIt, detailIt> configItr(configManager.configs.begin(), details.begin()); configItr.first != configManager.configs.end(); ++configItr.first) {
        if (!GLOBALS.cli.count(CLI_TAG))
            lastMonthYear = "";  // this fixes \n screen formatting for when -t is specified vs when it isn't
        
        if (!shouldDisplayConfig(*configItr.first, configManager))
            continue;
        
        if (!GLOBALS.cli.count(CLI_TAG)) {
            if (configItr.first != configManager.configs.begin() &&
                configManager.activeConfig > -1 && configManager.configs[configManager.activeConfig].temp)
                cout << "\n";

            displaySectionIntro(*configItr.first, *configItr.second, precisionLevel, statDetail);
        }
        
        /* faub configs */
        if (configItr.first->isFaub() && !configItr.first->fcache.empty()) {
            auto backupIt = configItr.first->fcache.getFirstBackup();
            auto ageIt = configItr.second->ages.begin();
            
            /* walk through faub backups */
            while (backupIt != configItr.first->fcache.getEnd()) {
                auto timeDetail = localtime(&backupIt->second.finishTime);
                string monthYear = backupIt->second.finishTime ? vars2MY(timeDetail->tm_mon+1, timeDetail->tm_year+1900) : "Unknown";

                // no tag is specified (i.e. everything is valid) or the specified tag matches
                if (!GLOBALS.cli.count(CLI_TAG) || tags.match(GLOBALS.cli[CLI_TAG].as<string>(), backupIt->first)) {
                    
                    if (NOTQUIET && monthYear != lastMonthYear) {
                        if (lastMonthYear.length())
                            cout << "\n";

                        faubTable.displayHeader(monthYear);
                    }

                    displayFaubLineDetails(faubTable, backupIt, tags, configItr.first->settings[sDOW].ivalue(), precisionLevel, statDetail, timeDetail, (*ageIt).length() ? *ageIt : "Unknown");
                    lastMonthYear = monthYear;
                    ++ageIt;
                }
                
                ++backupIt;
            }
        }
        
        /* single-file backup configs */
        else {
            colorRotator color;
            string lastMD5;
            
            for (auto &backup: configItr.first->cache.indexByFilename) {
               
                // lookup the the raw data detail
                auto rawIt = configItr.first->cache.rawData.find(backup.second);
                if (rawIt != configItr.first->cache.rawData.end()) {
                    string monthYear = vars2MY(rawIt->second.date_month, rawIt->second.date_year);
                    
                    // no tag is specified (i.e. everything is valid)
                    if (!GLOBALS.cli.count(CLI_TAG)) {
                        
                        if (NOTQUIET && monthYear != lastMonthYear) {
                            if (lastMonthYear.length())
                                cout << "\n";
                            
                            singleFileTable.displayHeader(monthYear);
                            lastMonthYear = monthYear;
                        }
                        
                        displaySingleLineDetails(rawIt, configItr.first->cache, singleFileTable, color, lastMD5, configItr.first->settings[sDOW].ivalue(), precisionLevel, statDetail);
                        lastMD5 = rawIt->second.md5;
                    }
                }
            }
        }
        
        // only increment the "detail" pointer if we do the loop, not when we skip iterations ("continue")
        ++configItr.second;
    }
}
