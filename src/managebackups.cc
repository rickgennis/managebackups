
#include "syslog.h"
#include "unistd.h"
#include <sys/stat.h>
#include <pcre++.h>
#include <iostream>
#include <filesystem>
#include <dirent.h>
#include <time.h>

#include "BackupEntry.h"
#include "BackupCache.h"
#include "BackupConfig.h"
#include "ConfigManager.h"
#include "cxxopts.hpp"
#include "globalsdef.h"
#include "colors.h"
#include "statistics.h"
#include "util_generic.h"
#include "help.h"
#include "PipeExec.h"


using namespace pcrepp;
struct global_vars GLOBALS;


void parseDirToCache(string directory, string fnamePattern, BackupCache& cache) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    Pcre fnameRE(fnamePattern);
    Pcre tempRE("\\.tmp\\.\\d+$");

    if ((c_dir = opendir(directory.c_str())) != NULL ) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, "..") ||
                    tempRE.search(string(c_dirEntry->d_name)))
                continue;

            string fullFilename = addSlash(directory) + string(c_dirEntry->d_name);

            ++GLOBALS.statsCount;
            struct stat statData;
            if (!stat(fullFilename.c_str(), &statData)) {

                // recurse into subdirectories
                if ((statData.st_mode & S_IFMT) == S_IFDIR) {
                    parseDirToCache(fullFilename, fnamePattern, cache);
                }
                else {
                    // filter for filename if specified
                    if (fnamePattern.length() && !fnameRE.search(string(c_dirEntry->d_name))) {
                        DEBUG(4, "skipping due to name mismatch: " << fullFilename);
                        continue;
                    }

                    // if the cache has an existing md5 and the cache's mtime and size match
                    // what we just read from disk, consider the cache valid.  only update
                    // the inode & age info. 
                    BackupEntry *pCacheEntry;
                    if ((pCacheEntry = cache.getByFilename(fullFilename)) != NULL) {
                        if (pCacheEntry->md5.length() && 
                           pCacheEntry->mtime &&
                           pCacheEntry->mtime == statData.st_mtime &&
                           pCacheEntry->size == statData.st_size) {

                            pCacheEntry->links = statData.st_nlink;
                            pCacheEntry->inode = statData.st_ino;

                            cache.addOrUpdate(*pCacheEntry->updateAges(GLOBALS.startupTime), true);
                            continue;
                        }
                    }

                    // otherwise let's update the cache with everything we just read and
                    // then calculate a new md5
                    BackupEntry cacheEntry;
                    cacheEntry.filename = fullFilename;
                    cacheEntry.links = statData.st_nlink;
                    cacheEntry.mtime = statData.st_mtime;
                    cacheEntry.inode = statData.st_ino;
                    cacheEntry.size = statData.st_size;
                    cacheEntry.updateAges(GLOBALS.startupTime);
                    cacheEntry.calculateMD5();
                    ++GLOBALS.md5Count;

                    cache.addOrUpdate(cacheEntry, true);
                }
            }
            else 
                log("error: unable to stat " + fullFilename);
        }
        closedir(c_dir);
    }
}


void scanConfigToCache(BackupConfig& config) {
    string directory = "";
    string fnamePattern = "";
    if (config.settings[sDirectory].value.length())
        directory = config.settings[sDirectory].value;

    // if there's a fnamePattern convert it into a wildcard version to match
    // backups with a date/time inserted.  i.e.
    //    myBigBackup.tgz --> myBigBackup*.tgz
    if (config.settings[sBackupFilename].value.length()) {
        Pcre regEx("(.*)\\.([^.]+)$");
        
        if (regEx.search(config.settings[sBackupFilename].value) && regEx.matches()) 
            fnamePattern = regEx.get_match(0) + DATE_REGEX + ".*" + regEx.get_match(1);
    }
    else 
        fnamePattern = DATE_REGEX;

    config.cache.scanned = true;
    parseDirToCache(directory, fnamePattern, config.cache);
}


BackupConfig* selectOrSetupConfig(ConfigManager &configManager) {
    string title;
    BackupConfig tempConfig(true);
    BackupConfig* currentConf = &tempConfig; 
    bool bSave = GLOBALS.cli.count(CLI_SAVE);
    bool bTitle = GLOBALS.cli.count(CLI_TITLE);
    bool bStats = GLOBALS.cli.count(CLI_STATS1) || GLOBALS.cli.count(CLI_STATS2);

    // if --title is specified on the command line set the active config
    if (bTitle) {
        if (int configNumber = configManager.config(GLOBALS.cli[CLI_TITLE].as<string>())) {
            configManager.activeConfig = configNumber - 1;
            currentConf = &configManager.configs[configManager.activeConfig];
        }
        else if (!bSave && bStats) {
            cerr << RED << "error: title not found; try -1 or -2 with no title to see all backups" << RESET << endl;
            exit(1);
        }

        currentConf->loadConfigsCache();
    }
    else {
        if (bSave) {
            cerr << RED << "error: --title must be specified in order to --save settings" << RESET << endl;
            exit(1);
        }

        if (bStats)
            configManager.loadAllConfigCaches();
    }

    // if any other settings are given on the command line, incorporate them into the selected config.
    // that config will be the one found from --title above (if any), or a temp config comprised only of defaults
    for (auto setting_it = currentConf->settings.begin(); setting_it != currentConf->settings.end(); ++setting_it) 
        if (GLOBALS.cli.count(setting_it->display_name)) {
            switch (setting_it->data_type) {

                case INT:  
                    if (bSave && (setting_it->value != to_string(GLOBALS.cli[setting_it->display_name].as<int>())))
                        currentConf->modified = 1;
                    setting_it->value = to_string(GLOBALS.cli[setting_it->display_name].as<int>());
                    break; 

                case STRING:
                    default:
                    if (bSave && (setting_it->value != GLOBALS.cli[setting_it->display_name].as<string>()))
                        currentConf->modified = 1;
                    setting_it->value = GLOBALS.cli[setting_it->display_name].as<string>();
                    break;

                case BOOL:
                    if (bSave && (setting_it->value != to_string(GLOBALS.cli[setting_it->display_name].as<bool>())))
                        currentConf->modified = 1;
                    setting_it->value = to_string(GLOBALS.cli[setting_it->display_name].as<bool>());
                    break;
            }
        }

    // convert --fp (failsafe paranoid) to its separate settings
    if (GLOBALS.cli.count(CLI_FS_FP)) {
        if (bSave && 
                (currentConf->settings[sFailsafeDays].value != "2" ||
                 currentConf->settings[sFailsafeBackups].value != "1"))
            currentConf->modified = 1;

        currentConf->settings[sFailsafeDays].value = "2";
        currentConf->settings[sFailsafeBackups].value = "1";
    }

    if (currentConf == &tempConfig) {
        if (bTitle && bSave) {
            Pcre search1("[\\s#;\\/\\\\]+", "g");
            Pcre search2("[\\?\\!]+", "g");

            string tempStr = search1.replace(tempConfig.settings[sTitle].value, "_");
            tempConfig.config_filename = addSlash(CONF_DIR) +search2.replace(tempStr, "") + ".conf";
            tempConfig.temp = false;
        }

        // if we're using a new/temp config, insert it into the list for configManager
        configManager.configs.insert(configManager.configs.end(), tempConfig);
        configManager.activeConfig = configManager.configs.size() - 1;
        currentConf = &configManager.configs[configManager.activeConfig];
    }

    if (!bStats && !currentConf->settings[sDirectory].value.length()) {
        cerr << RED << "error: --directory is required" << RESET << endl;
        exit(1);
    }

    return(currentConf);
}




void pruneBackups(BackupConfig& config) {
    Pcre regEnabled("^(t|true|y|yes|1)$", "i");

    if (GLOBALS.cli.count(CLI_NOPRUNE))
        return;

    if (!config.settings[sPruneLive].value.length() || !regEnabled.search(config.settings[sPruneLive].value)) {
        cout << RED << "warning: While a core feature, managebackups doesn't prune old backups" << endl;
        cout << "until specifically enabled.  Use --prune to enable pruning.  Use --prune" << endl;
        cout << "and --save to make it the default behavior for this backup configuration." << endl;
        cout << "pruning skipped;  would have used these settings:" << endl;
        cout << "\t--days " << config.settings[sDays].value << " --weeks " << config.settings[sWeeks].value;
        cout << " --months " << config.settings[sMonths].value << " --years " << config.settings[sYears].value << RESET << endl;
        return;
    }

    // failsafe checks
    int fb = stoi(config.settings[sFailsafeBackups].value, NULL);
    int fd = stoi(config.settings[sFailsafeDays].value, NULL);
    auto fnameIdx = config.cache.indexByFilename;

    if (fb > 0 && fd > 0) {
        int minValidBackups = 0;

        for (auto fnameIdx_it = fnameIdx.begin(); fnameIdx_it != fnameIdx.end(); ++fnameIdx_it) {
            auto raw_it = config.cache.rawData.find(fnameIdx_it->second);

            if (raw_it != config.cache.rawData.end() && raw_it->second.day_age <= fd) {
                ++minValidBackups;
            }

            if (minValidBackups >= fb)
                break;
        }

        if (minValidBackups < fb) {
            string message = "skipping pruning due to failsafe check; only " + to_string(fb) +
                    " backup" + s(fb) + " within the last " + to_string(fd) + " day" + s(fd);
            
            cerr << RED << "warning: " << message << RESET << endl;
            log("[" + config.settings[sTitle].value + "] " + message);
            return;
        }
    }

    set<string> changedMD5s;

    // loop through the filename index sorted by filename (i.e. all backups by age)
    for (auto fnameIdx_it = fnameIdx.begin(); fnameIdx_it != fnameIdx.end(); ++fnameIdx_it) {
        auto raw_it = config.cache.rawData.find(fnameIdx_it->second);

        if (raw_it != config.cache.rawData.end()) { 
            int filenameAge = raw_it->second.day_age;
            int filenameDOW = raw_it->second.dow;

            // daily
            if (filenameAge <= config.settings[sDays].ivalue()) {
                DEBUG(2, "keep_daily: " << raw_it->second.filename << " (age=" << filenameAge << ", dow=" << dw(filenameDOW) << ")");
                continue;
            }

            // weekly
            if (filenameAge / 7 <= config.settings[sWeeks].ivalue() && filenameDOW == 0) {
                DEBUG(2, "keep_weekly: " << raw_it->second.filename << " (age=" << filenameAge << ", dow=" << dw(filenameDOW) << ")");
                continue;
            }

            // monthly
            auto monthLimit = config.settings[sMonths].ivalue();
            if (monthLimit && raw_it->second.month_age <= monthLimit && raw_it->second.date_day == 1) {
                DEBUG(2, "keep_monthly: " << raw_it->second.filename << " (age=" << filenameAge << ", dow=" << dw(filenameDOW) << ")");
                continue;
            }

            // yearly
            auto yearLimit = config.settings[sYears].ivalue();
            if (yearLimit && filenameAge / 365 <= yearLimit && raw_it->second.date_day == 1 && raw_it->second.date_day == 1) {
                DEBUG(2, "\tkeep_yearly: " << raw_it->second.filename << " (age=" << filenameAge << ", dow=" << dw(filenameDOW) << ")");
                continue;
            }

            if (GLOBALS.cli.count(CLI_TEST))
                cout << YELLOW << "[" << config.settings[sTitle].value << "] TESTMODE: would have deleted " <<
                    raw_it->second.filename << " (age=" + to_string(filenameAge) << ", dow=" + dw(filenameDOW) <<
                    ")" << RESET << endl;
            else {

                // delete the file and remove it from all caches
                if (!unlink(raw_it->second.filename.c_str())) {
                    NOTQUIET && cout << "removed " << raw_it->second.filename << endl; 
                    log("[" + config.settings[sTitle].value + "] removing " + raw_it->second.filename + 
                        " (age=" + to_string(filenameAge) + ", dow=" + dw(filenameDOW) + ")");

                    changedMD5s.insert(raw_it->second.md5);
                    config.cache.remove(raw_it->second);
                }
            }
        }
    }

    if (!GLOBALS.cli.count(CLI_TEST)) {
        config.removeEmptyDirs();

        for (auto md5_it = changedMD5s.begin(); md5_it != changedMD5s.end(); ++md5_it)
            config.cache.reStatMD5(*md5_it);
    }
}


void updateLinks(BackupConfig& config) {
    unsigned int maxLinksAllowed = config.settings[sMaxLinks].ivalue();
    bool includeTime = config.settings[sIncTime].ivalue();
    bool rescanRequired;
    
    /* The indexByMD5 is a list of lists ("map" of "set"s).  Here we loop through the list of MD5s
     * (the map) once.  For each MD5, we loop through its list of associated files (the set) twice:
     *     - 1st time to find the file with the greatest number of existing hard links (call this the reference file)
     *     - 2nd time to relink any individual file to the reference file
     * There are a number of caveats.  We don't relink any file that's less than a day old because it may be being
     * updated.  And we also account for the configured max number of hard links.  If that number is reached a subsequent
     * grouping of linked files is started.
     */

    if (maxLinksAllowed < 2)
        return;

    // loop through list of MD5s (the map)
    for (auto md5_it = config.cache.indexByMD5.begin(); md5_it != config.cache.indexByMD5.end(); ++md5_it) {
        
        // only consider md5s with more than one file associated
        if (md5_it->second.size() < 2)
            continue;

        do {
            /* the rescan loop is a special case where we've hit maxLinksAllowed.  there may be more files
             * with that same md5 still to be linked, which would require a new inode.  for that we need a 
             * new reference file.  setting rescanrequired to true takes up back here to pick a new one.  */
             
            rescanRequired = false;

            // find the file matching this md5 with the greatest number of links.
            // some files matching this md5 may already be linked together while others
            // are not.  we need the one with the greatest number of links so that
            // we don't constantly relink the same files due to random selection.
            BackupEntry *referenceFile = NULL;
            unsigned int maxLinksFound = 0;

            DEBUG(5, "top of scan for " << md5_it->first);

            // 1st time: loop through the list of files (the set)
            for (auto refFile_it = md5_it->second.begin(); refFile_it != md5_it->second.end(); ++refFile_it) {
                auto refFileIdx = *refFile_it;
                auto raw_it = config.cache.rawData.find(refFileIdx);

                DEBUG(5, "considering " << raw_it->second.filename << " (" << raw_it->second.links << ")");
                if (raw_it != config.cache.rawData.end()) {

                    if (raw_it->second.links > maxLinksFound &&            // more links than previous files for this md5
                            raw_it->second.links < maxLinksAllowed &&      // still less than the configured max
                            raw_it->second.day_age) {                      // at least a day old (i.e. don't relink today's file)

                        referenceFile = &raw_it->second;
                        maxLinksFound = raw_it->second.links;

                        DEBUG(5, "new ref file " << referenceFile->md5 << " " << referenceFile->filename << "; links=" << maxLinksFound);
                    }
                }
            }

            // the first actionable file (one that's not today's file and doesn't already have links at the maxLinksAllowed level)
            // becomes the reference file.  if there's no reference file there's nothing to do for this md5.  skip to the next one.
            if (referenceFile == NULL) {
                DEBUG(5, "no reference file found for " << md5_it->first);
                continue;
            }

            // 2nd time: loop through the list of files (the set)
            for (auto refFile_it = md5_it->second.begin(); refFile_it != md5_it->second.end(); ++refFile_it) {
                auto refFileIdx = *refFile_it;
                auto raw_it = config.cache.rawData.find(refFileIdx);
                DEBUG(5, "\texamining " << raw_it->second.filename);

                if (raw_it != config.cache.rawData.end()) {

                    // skip the reference file; can't relink it to itself
                    if (referenceFile == &raw_it->second) {
                        DEBUG(5, "\t\treference file itself");
                        continue;
                    }

                    // skip files that are already linked
                    if (referenceFile->inode == raw_it->second.inode) {
                        DEBUG(5, "\t\talready linked");
                        continue;
                    }

                    // skip today's file as it could still be being updated
                    if (!raw_it->second.day_age && !includeTime) {
                        DEBUG(5, "\t\ttoday's file");
                        continue;            
                    }

                    // skip if this file already has the max links
                    if (raw_it->second.links >= maxLinksAllowed) {
                        DEBUG(5, "\t\tfile links already maxed out");
                        continue;            
                    }

                    // relink the file to the reference file
                    string detail = raw_it->second.filename + " <-> " + referenceFile->filename;
                    if (GLOBALS.cli.count(CLI_TEST))
                        cout << YELLOW << "[" << config.settings[sTitle].value << "] TESTMODE: would have linked " << detail << RESET << endl;
                    else {
                        if (!unlink(raw_it->second.filename.c_str())) {
                            if (!link(referenceFile->filename.c_str(), raw_it->second.filename.c_str())) {
                                cout << "linked " << detail << endl;
                                log("[" + config.settings[sTitle].value + "] linked " + detail);

                                config.cache.reStatMD5(md5_it->first);

                                if (referenceFile->links >= maxLinksAllowed) {
                                    rescanRequired = true;
                                    break;
                                }
                            }
                            else {
                                cerr << RED << "error: unable to link " << detail << " (" << strerror(errno) << ")" << RESET << endl;
                                log("[" + config.settings[sTitle].value + "] error: unable to link " + detail);
                            }
                        }
                        else {
                            cerr << RED << "error: unable to remove" << raw_it->second.filename << " in prep to link it (" 
                                << strerror(errno) << ")" << RESET << endl;
                            log("[" + config.settings[sTitle].value + "] error: unable to remove " + raw_it->second.filename + 
                                " in prep to link it (" + strerror(errno) + ")");
                        }
                    }
                }
            }
        } while (rescanRequired);    
    }    
}



void performBackup(BackupConfig& config) {
    bool incTime = GLOBALS.cli.count(CLI_TIME);
    string setFname = config.settings[sBackupFilename].value;
    string setDir = config.settings[sDirectory].value;
    string tempExtension = ".tmp." + to_string(GLOBALS.pid);

    if (!setFname.length() || GLOBALS.cli.count(CLI_NOBACKUP))
        return;

    // setup path names and filenames
    time_t now;
    char buffer[100];
    now = time(NULL);

    strftime(buffer, sizeof(buffer), incTime ? "%Y/%m/%d/": "%Y/%m/", localtime(&now));
    string subDir = buffer;
        
    strftime(buffer, sizeof(buffer), incTime ? "-%Y%m%d-%H:%M:%S": "-%Y%m%d", localtime(&now));
    string fnameInsert = buffer;

    string fullDirectory = addSlash(setDir) + subDir;
    string backupFilename;

    Pcre fnamePartsRE("(.*)(\\.[^.]+)$");
    if (fnamePartsRE.search(setFname) && fnamePartsRE.matches() > 1) 
        backupFilename = fullDirectory + fnamePartsRE.get_match(0) + fnameInsert + fnamePartsRE.get_match(1);
    else
        backupFilename = fullDirectory +  setFname + fnameInsert;

    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW << "[" << config.settings[sTitle].value << "] TESTMODE: would have begun backup to " <<
            backupFilename << RESET << endl;
        return;
    }

    // make sure the destination directory exists
    mkdirp(fullDirectory);

    log("[" + config.settings[sTitle].value + "] starting backup to " + backupFilename);

    // note start time
    time_t startTime;
    time(&startTime);

    // begin backing up
    PipeExec proc(config.settings[sBackupCommand].value);
    proc.execute2file(backupFilename + tempExtension);

    // note finish time
    time_t finishTime;
    time(&finishTime);

    struct stat statData;
    if (!stat(string(backupFilename + tempExtension).c_str(), &statData)) {
        if (statData.st_size >= GLOBALS.minBackupSize) {
            if (!rename(string(backupFilename + tempExtension).c_str(), backupFilename.c_str())) {
                log("[" + config.settings[sTitle].value + "] completed backup to " + backupFilename + " in " + 
                        timeDiff(startTime, finishTime, 3));
                NOTQUIET && cout << "\t• successfully backed up to " << backupFilename << endl;

                BackupEntry cacheEntry;
                cacheEntry.filename = backupFilename;
                cacheEntry.links = statData.st_nlink;
                cacheEntry.mtime = statData.st_mtime;
                cacheEntry.inode = statData.st_ino;
                cacheEntry.size = statData.st_size;
                cacheEntry.duration = finishTime - startTime;
                cacheEntry.updateAges(finishTime);
                cacheEntry.calculateMD5();

                config.cache.addOrUpdate(cacheEntry, true);
                config.cache.reStatMD5(cacheEntry.md5);
            }
            else {
                log("[" + config.settings[sTitle].value + "] backup failed, unable to rename temp file to " + backupFilename);
                unlink(string(backupFilename + tempExtension).c_str());
                NOTQUIET && cout << "\t" << RED << "• backup failed to " << backupFilename << RESET << endl;
            }
        }
        else {
            log("[" + config.settings[sTitle].value + "] backup failed to " + backupFilename + " (insufficient output/size)");
            NOTQUIET && cout << "\t" << RED << "• backup failed to " << backupFilename << " (insufficient output/size)" << RESET << endl;
        }
    }
    else {
        log("[" + config.settings[sTitle].value + "] backup command failed to generate any output");
        NOTQUIET && cout << "\t" << RED << "• backup failed to generate any output" << RESET << endl;
    }
}


int main(int argc, char *argv[]) {
    GLOBALS.statsCount = 0;
    GLOBALS.md5Count = 0;
    GLOBALS.pid = getpid();
    GLOBALS.minBackupSize = 1500;

    time(&GLOBALS.startupTime);
    openlog("managebackups", LOG_PID | LOG_NDELAY, LOG_LOCAL1);
    cxxopts::Options options("managebackups", "Create and manage backups");

    options.add_options()
        (string("t,") + CLI_TITLE, "Title", cxxopts::value<std::string>())
        (string("d,") + CLI_DAYS, "Days", cxxopts::value<int>())
        (string("w,") + CLI_WEEKS, "Weeks", cxxopts::value<int>())
        (string("m,") + CLI_MONTHS, "Months", cxxopts::value<int>())
        (string("y,") + CLI_YEARS, "Years", cxxopts::value<int>())
        (string("f,") + CLI_FILE, "Filename", cxxopts::value<std::string>())
        (string("c,") + CLI_COMMAND, "Command", cxxopts::value<std::string>())
        (string("n,") + CLI_NOTIFY, "Notify", cxxopts::value<std::string>())
        (string("v,") + CLI_VERBOSE, "Verbose output", cxxopts::value<bool>()->default_value("false"))
        (string("q,") + CLI_QUIET, "No output", cxxopts::value<bool>()->default_value("false"))
        (string("l,") + CLI_MAXLINKS, "Max hard links", cxxopts::value<int>())
        (CLI_SAVE, "Save config", cxxopts::value<bool>()->default_value("false"))
        (CLI_FS_BACKUPS, "Failsafe Backups", cxxopts::value<int>())
        (CLI_FS_DAYS, "Failsafe Days", cxxopts::value<int>())
        (CLI_FS_FP, "Failsafe Paranoid", cxxopts::value<bool>()->default_value("false"))
        (CLI_DIR, "Directory", cxxopts::value<std::string>())
        (CLI_COPYTO, "Copy to", cxxopts::value<std::string>())
        (CLI_SFTPTO, "SFTP to", cxxopts::value<std::string>())
        (CLI_STATS1, "Stats summary", cxxopts::value<bool>()->default_value("false"))
        (CLI_STATS2, "Stats detail", cxxopts::value<bool>()->default_value("false"))
        (CLI_PRUNE, "Enable pruning", cxxopts::value<bool>()->default_value("false"))
        (CLI_NOPRUNE, "Disable pruning", cxxopts::value<bool>()->default_value("false"))
        (CLI_TEST, "Test only mode", cxxopts::value<bool>()->default_value("false"))
        (CLI_DEFAULTS, "Show defaults", cxxopts::value<bool>()->default_value("false"))
        (CLI_TIME, "Include time", cxxopts::value<bool>()->default_value("false"))
        (CLI_NOBACKUP, "Don't backup", cxxopts::value<bool>()->default_value("false"))
        (CLI_NOCOLOR, "Disable color", cxxopts::value<bool>()->default_value("false"));

    try {
        GLOBALS.cli = options.parse(argc, argv);
        GLOBALS.debugLevel = GLOBALS.cli.count(CLI_VERBOSE);
        GLOBALS.color = !GLOBALS.cli[CLI_NOCOLOR].as<bool>();
    }
    catch (cxxopts::OptionParseException& e) {
        cerr << "managebackups: " << e.what() << endl;
        exit(1);
    }

    if (argc == 1) {
        showHelp(hSyntax);
        exit(0);
    }

    if (GLOBALS.cli.count(CLI_DEFAULTS)) {
        showHelp(hDefaults);
        exit(0);
    }

    ConfigManager configManager;
    auto currentConfig = selectOrSetupConfig(configManager);

    if (GLOBALS.cli.count(CLI_STATS1) || GLOBALS.cli.count(CLI_STATS2)) {
        displayStats(configManager);
        exit(0);
    }

    scanConfigToCache(*currentConfig);
    pruneBackups(*currentConfig);           // only run after a scan
    updateLinks(*currentConfig);
    performBackup(*currentConfig);

    DEBUG(1, "stats: " << GLOBALS.statsCount << ", md5s: " << GLOBALS.md5Count);

    return 0;
}
