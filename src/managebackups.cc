 
#include "syslog.h"
#include "unistd.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <signal.h>
#include <pcre++.h>
#include <iostream>
#include <filesystem>
#include <dirent.h>
#include <time.h>
#include <sys/types.h>
#include <pwd.h>

#ifdef __APPLE__
    #include <sys/param.h>
    #include <sys/mount.h>
#else
    #include <sys/statfs.h>
#endif

#include "BackupEntry.h"
#include "BackupCache.h"
#include "BackupConfig.h"
#include "ConfigManager.h"
#include "cxxopts.hpp"
#include "globalsdef.h"
#include "colors.h"
#include "statistics.h"
#include "util_generic.h"
#include "notify.h"
#include "help.h"
#include "PipeExec.h"
#include "setup.h"


using namespace pcrepp;
struct global_vars GLOBALS;

struct methodStatus {
    bool success;
    string detail;

    methodStatus() { success = true; }
    methodStatus(bool s, string d) { success = s; detail = d; }
};


/*******************************************************************************
 * parseDirToCache(directory, fnamePattern, cache)
 *
 * Recursively walk the given directory looking for all files that match the
 * fnamePattern (all directories are considered, regardless of their name matching
 * the pattern). For all matching filenames, compare them to the given cache.
 *
 *  (a) if a file is already in the cache and its mtime and size on disk match
 *      the cache, update the cache with its inode number, links and ages
 *
 *  (b) if a file is already in the cache but doesn't match mtime or size on disk
 *      update everything in the cache, most noteably, calcuclate its md5
 *
 *  (c) if a file isn't in the cache create the entry and treat it as (b) above
 *******************************************************************************/
void parseDirToCache(string directory, string fnamePattern, BackupCache& cache) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    Pcre fnameRE(fnamePattern);
    Pcre tempRE("\\.tmp\\.\\d+$");
    bool testMode = GLOBALS.cli.count(CLI_TEST);

    if ((c_dir = opendir(directory.c_str())) != NULL ) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
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
                        DEBUG(4, "skip name mismatch: " << fullFilename);
                        continue;
                    }

                    if (tempRE.search(string(c_dirEntry->d_name))) {
                        DEBUG(2, "in process file found (" << c_dirEntry->d_name << ")");
                        cache.inProcess = true;
                        continue;
                    }

                    // if the cache has an existing md5 and the cache's mtime and size match
                    // what we just read from disk, consider the cache valid.  only update
                    // the inode & age info. 
                    BackupEntry *pCacheEntry;
                    if ((pCacheEntry = cache.getByFilename(fullFilename)) != NULL) {
                        if (pCacheEntry->md5.length() && pCacheEntry->size == statData.st_size &&
                           pCacheEntry->mtime && pCacheEntry->mtime == statData.st_mtime) {

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

                    if (cacheEntry.calculateMD5()) {
                        cache.addOrUpdate(cacheEntry, true, true);
                    }
                    else {
                        log("error: unable to read " + fullFilename);
                        SCREENERR("error: unable to read " << fullFilename);
                    }
                }
            }
            else {
                // stat errors can happen if two copies of managebackups are running concurrently
                // and the other instance deletes the file between our readdir() and stat().  that's
                // legit and normal. I can't think of a situation where we'd have perms to readdir()
                // but not to stat(). so that may be the only scenario where we could land in this
                // block. in which case its not worth reporting. it would just be a distraction.
                /*
                log("error: unable to stat " + fullFilename);
                SCREENERR("error: unable to stat " << fullFilename);
                */
            }
        }
        closedir(c_dir);
    }
    else
        SCREENERR("error: unable to read " << directory);
}


/*******************************************************************************
 * scanConfigToCache(config)
 *
 * Wrapper function for parseDirToCache().
 *******************************************************************************/
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

    parseDirToCache(directory, fnamePattern, config.cache);
}


/*******************************************************************************
 * selectOrSetupConfig(configManager)
 *
 * Determine which config/profile the user has selected and load it. If no
 * profile was selected created a temp one that will be used, but not persisted
 * to disk. Validate required commandline options.
 *******************************************************************************/
BackupConfig* selectOrSetupConfig(ConfigManager &configManager) {
    string profile;
    BackupConfig tempConfig(true);
    BackupConfig* currentConf = &tempConfig; 
    bool bSave = GLOBALS.cli.count(CLI_SAVE);
    bool bProfile = GLOBALS.cli.count(CLI_PROFILE);

    // if --profile is specified on the command line set the active config
    if (bProfile) {
        if (int configNumber = configManager.config(GLOBALS.cli[CLI_PROFILE].as<string>())) {
            configManager.activeConfig = configNumber - 1;
            currentConf = &configManager.configs[configManager.activeConfig];
        }
        else if (!bSave) {
            SCREENERR("error: profile not found; try -0 or -1 with no profile to see all backups\n" <<
                    "or use --save to create this profile.");
            exit(1);
        }

        currentConf->loadConfigsCache();
    }
    else {
        if (bSave) {
            SCREENERR("error: --profile must be specified in order to --save settings.\n" 
                << "Once saved --profile becomes a macro for all settings specified with the --save.\n"
                << "For example, these two commands would do the same things:\n\n"
                << "\tmanagebackups --profile myback --directory /etc --file etc.tgz --daily 5 --save\n"
                << "\tmanagebackups --profile myback\n\n"
                << "Options specified with a profile that's aleady been saved will override that\n"
                << "option for this run only (unless --save is given again).");
            exit(1);
        }

        if (GLOBALS.stats || GLOBALS.cli.count(CLI_ALL))
            configManager.loadAllConfigCaches();
        else 
            currentConf->loadConfigsCache();
    }

    // if any other settings are given on the command line, incorporate them into the selected config.
    // that config will be the one found from --profile above (if any), or a temp config comprised only of defaults
    for (auto &setting: currentConf->settings)
        if (GLOBALS.cli.count(setting.display_name)) {
            DEBUG(5, "command line param: " << setting.display_name << " (" << setting.data_type << ")");

            switch (setting.data_type) {

                case INT:  
                    if (bSave && (setting.value != to_string(GLOBALS.cli[setting.display_name].as<int>())))
                        currentConf->modified = 1;
                    setting.value = to_string(GLOBALS.cli[setting.display_name].as<int>());
                    break; 

                case STRING:
                    default:
                    if (bSave && (setting.value != GLOBALS.cli[setting.display_name].as<string>()))
                        currentConf->modified = 1;
                    setting.value = GLOBALS.cli[setting.display_name].as<string>();
                    break;

                case OCTAL:
                    if (bSave && (setting.value != GLOBALS.cli[setting.display_name].as<string>()))
                        currentConf->modified = 1;
                    try {
                        setting.value = GLOBALS.cli[setting.display_name].as<string>();
                        auto ignored = stol(setting.value, NULL, 8);  // throws on error
                    }
                    catch (...) {
                        log("error: invalid octal value specified for --" + setting.display_name + " (" + setting.value + ")");
                        SCREENERR("error: invalid octal value specified for option" <<
                                "\n\t--" << setting.display_name << " " << setting.value);
                        exit(1);
                    }
                    break;

                case SIZE:
                    if (bSave && (setting.value != GLOBALS.cli[setting.display_name].as<string>()))
                        currentConf->modified = 1;
                    try {
                        setting.value = GLOBALS.cli[setting.display_name].as<string>();
                        auto ignored = approx2bytes(setting.value);  // throws on error
                    }
                    catch (...) {
                        log("error: invalid size value specified for --" + setting.display_name + " (" + setting.value + ")");
                        SCREENERR("error: invalid size value specified for option" << 
                                "\n\t--" << setting.display_name << " " << setting.value);
                        exit(1);
                    }
                    break;

                case BOOL:
                    if (bSave && (setting.value != to_string(GLOBALS.cli[setting.display_name].as<bool>())))
                        currentConf->modified = 1;
                    setting.value = to_string(GLOBALS.cli[setting.display_name].as<bool>());
                    break;
            }
        }

    // apply fp from the config file, if set
    if (str2bool(currentConf->settings[sFP].value)) {
        currentConf->settings[sFailsafeDays].value = "2";
        currentConf->settings[sFailsafeBackups].value = "1";
    }

    // convert --fp (failsafe paranoid) to its separate settings
    if (GLOBALS.cli.count(CLI_FS_FP)) {

        if (GLOBALS.cli.count(CLI_FS_DAYS) || GLOBALS.cli.count(CLI_FS_BACKUPS)) {
            SCREENERR("error: --fp is mutually exclusive with --fs_days & --fs_backups");
            exit(1);
        }

        if (bSave && 
                (currentConf->settings[sFailsafeDays].value != "2" ||
                 currentConf->settings[sFailsafeBackups].value != "1"))
            currentConf->modified = 1;

        currentConf->settings[sFailsafeDays].value = "2";
        currentConf->settings[sFailsafeBackups].value = "1";
    }

    if (currentConf == &tempConfig) {
        if (bProfile && bSave) {
            tempConfig.config_filename = addSlash(GLOBALS.confDir) + safeFilename(tempConfig.settings[sTitle].value) + ".conf";
            tempConfig.temp = false;
        }

        // if we're using a new/temp config, insert it into the list for configManager
        configManager.configs.insert(configManager.configs.end(), tempConfig);
        configManager.activeConfig = configManager.configs.size() - 1;
        currentConf = &configManager.configs[configManager.activeConfig];
    }

    // user doesn't want to show stats
    if (!GLOBALS.stats && 

            // the current profile (either via -p or full config spelled out in CLI) doesn't have a dir specified
            !currentConf->settings[sDirectory].value.length() &&

            // user hasn't selected --all and there's a dir configured in at least one (first) profile
            !(GLOBALS.cli.count(CLI_ALL) && configManager.configs.size() && configManager.configs[0].settings[sDirectory].value.length())) {

        SCREENERR("error: --directory is required");
        exit(1);
    }

    return(currentConf);
}




/*******************************************************************************
 * pruneBackups(config)
 *
 * Apply the full rentetion policy inclusive of all safety checks.
 *******************************************************************************/
void pruneBackups(BackupConfig& config) {
    if (GLOBALS.cli.count(CLI_NOPRUNE))
        return;

    if (!config.settings[sPruneLive].value.length() && !GLOBALS.cli.count(CLI_NOPRUNE) && !GLOBALS.cli.count(CLI_QUIET)) {
        SCREENERR("warning: While a core feature, managebackups doesn't prune old backups" 
            << "until specifically enabled.  Use --prune to enable pruning.  Use --prune"
            << "and --save to make it the default behavior for this profile."
            << "pruning skipped;  would have used these settings:"
            << "\t--days " << config.settings[sDays].value << " --weeks " << config.settings[sWeeks].value
            << " --months " << config.settings[sMonths].value << " --years " << config.settings[sYears].value);
        return;
    }

    // failsafe checks
    int fb = config.settings[sFailsafeBackups].ivalue();
    int fd = config.settings[sFailsafeDays].ivalue();

    if (fb > 0 && fd > 0) {
        int minValidBackups = 0;

        for (auto &fnameIdx: config.cache.indexByFilename) {
            auto raw_it = config.cache.rawData.find(fnameIdx.second);

            DEBUG(5, "fs: " << raw_it->second.filename << " (" << raw_it->second.day_age << ")");
            if (raw_it != config.cache.rawData.end() && raw_it->second.day_age <= fd) {
                ++minValidBackups;
            }

            if (minValidBackups >= fb)
                break;
        }

        if (minValidBackups < fb) {
            string message = "skipping pruning due to failsafe check; only " + to_string(minValidBackups) +
                    " backup" + s(minValidBackups) + " within the last " + to_string(fd) + " day" + s(fd) + "; " + to_string(fb) + " required";
            
            SCREENERR("warning: " << message);
            log(config.ifTitle() + " " + message);
            return;
        }
    }

    set<string> changedMD5s;
    DEBUG(4, "weeklies set to dow " << config.settings[sDOW].ivalue());

    // loop through the filename index sorted by filename (i.e. all backups by age)
    for (auto fIdx_it = config.cache.indexByFilename.begin(), next_it = fIdx_it; fIdx_it != config.cache.indexByFilename.end(); fIdx_it = next_it) {

        // the second iterator (next_it) is necessary because a function called within 
        // this loop (config.cache.remove()) calls erase() on our primary iterator. while
        // that appears to be handled on darwin it crashes under linux. next_it allows 
        // the loop to track the next value for the iterator without dereferencing a deleted pointer.
        ++next_it;

        DEBUG(5, "considering " << fIdx_it->first);
        auto raw_it = config.cache.rawData.find(fIdx_it->second);

        if (raw_it != config.cache.rawData.end()) { 
            int filenameAge = raw_it->second.day_age;
            int filenameDOW = raw_it->second.dow;

            // daily
            if (filenameAge <= config.settings[sDays].ivalue()) {
                DEBUG(2, "keep_daily: " << raw_it->second.filename << " (age=" << filenameAge << ", dow=" << dw(filenameDOW) << ")");
                continue;
            }

            // weekly
            if (filenameAge / 7.0 <= config.settings[sWeeks].ivalue() && filenameDOW == config.settings[sDOW].ivalue()) {
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
            if (yearLimit && filenameAge / 365.0 <= yearLimit && raw_it->second.date_month == 1 && raw_it->second.date_day == 1) {
                DEBUG(2, "\tkeep_yearly: " << raw_it->second.filename << " (age=" << filenameAge << ", dow=" << dw(filenameDOW) << ")");
                continue;
            }

            if (GLOBALS.cli.count(CLI_TEST))
                cout << YELLOW << config.ifTitle() << " TESTMODE: would have deleted " <<
                    raw_it->second.filename << " (age=" + to_string(filenameAge) << ", dow=" + dw(filenameDOW) <<
                    ")" << RESET << endl;
            else {

                // delete the file and remove it from all caches
                if (!unlink(raw_it->second.filename.c_str())) {
                    NOTQUIET && cout << "\t• removed " << raw_it->second.filename << endl; 
                    log(config.ifTitle() + " removing " + raw_it->second.filename + 
                        " (age=" + to_string(filenameAge) + ", dow=" + dw(filenameDOW) + ")");

                    changedMD5s.insert(raw_it->second.md5);
                    config.cache.remove(raw_it->second);
                    DEBUG(4, "completed removal of file");
                }
            }
        }
    }

    if (!GLOBALS.cli.count(CLI_TEST)) {
        DEBUG(4, "removing empty directories");
        config.removeEmptyDirs();

        for (auto &md5: changedMD5s) {
            DEBUG(4, "re-stating " << md5);
            config.cache.reStatMD5(md5);
            DEBUG(4, "re-stat complete");
        }
    }
}


/*******************************************************************************
 * updateLinks(config)
 *
 * Hard link identical backups together to save space.
 *******************************************************************************/
void updateLinks(BackupConfig& config) {
    unsigned int maxLinksAllowed = config.settings[sMaxLinks].ivalue();
    bool includeTime = str2bool(config.settings[sIncTime].value);
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
    for (auto &md5: config.cache.indexByMD5) {
        
        // only consider md5s with more than one file associated
        if (md5.second.size() < 2)
            continue;

        do {
            /* the rescan loop is a special case where we've hit maxLinksAllowed.  there may be more files
             * with that same md5 still to be linked, which would require a new inode.  for that we need a 
             * new reference file.  setting rescanrequired to true takes us back here to pick a new one.  */
             
            rescanRequired = false;

            // find the file matching this md5 with the greatest number of links.
            // some files matching this md5 may already be linked together while others
            // are not.  we need the one with the greatest number of links so that
            // we don't constantly relink the same files due to random selection.
            BackupEntry *referenceFile = NULL;
            unsigned int maxLinksFound = 0;

            DEBUG(5, "top of scan for " << md5.first);

            // 1st time: loop through the list of files (the set)
            for (auto &fileID: md5.second) {
                auto raw_it = config.cache.rawData.find(fileID);

                DEBUG(5, "considering for ref file " << raw_it->second.filename << " (" << raw_it->second.links << ")");
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
                DEBUG(5, "no reference file found for " << md5.first);
                continue;
            }

            // 2nd time: loop through the list of files (the set)
            for (auto &fileID: md5.second) {
                auto raw_it = config.cache.rawData.find(fileID);
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
                        cout << YELLOW << config.ifTitle() << " TESTMODE: would have linked " << detail << RESET << endl;
                    else {
                        if (!unlink(raw_it->second.filename.c_str())) {
                            if (!link(referenceFile->filename.c_str(), raw_it->second.filename.c_str())) {
                                NOTQUIET && cout << "\t• linked " << detail << endl;
                                log(config.ifTitle() + " linked " + detail);

                                config.cache.reStatMD5(md5.first);

                                if (referenceFile->links >= maxLinksAllowed) {
                                    rescanRequired = true;
                                    break;
                                }
                            }
                            else {
                                SCREENERR("error: unable to link " << detail << " (" << strerror(errno) << ")");
                                log(config.ifTitle() + " error: unable to link " + detail);
                            }
                        }
                        else {
                            SCREENERR("error: unable to remove " << raw_it->second.filename << " in prep to link it (" 
                                << strerror(errno) << ")");
                            log(config.ifTitle() + " error: unable to remove " + raw_it->second.filename + 
                                " in prep to link it (" + strerror(errno) + ")");
                        }
                    }
                }
            }
        } while (rescanRequired);    
    }    
}


/*******************************************************************************
 * interpolate(command, subDir, fullDirectory, basicFilename)
 *
 * Substitute variable values into SCP/SFP commands.
 *******************************************************************************/
string interpolate(string command, string subDir, string fullDirectory, string basicFilename) {
    size_t pos;

    while ((pos = command.find(INTERP_SUBDIR)) != string::npos)
        command.replace(pos, string(INTERP_SUBDIR).length(), subDir);

    while ((pos = command.find(INTERP_FULLDIR)) != string::npos)
        command.replace(pos, string(INTERP_FULLDIR).length(), fullDirectory);

    while ((pos = command.find(INTERP_FILE)) != string::npos)
        command.replace(pos, string(INTERP_FILE).length(), basicFilename);

    return(command);
}
 

/*******************************************************************************
 * sCpBackup(config, backupFilename, subDir, sCpParams)
 *
 * SCP the backup to the specified location.
 *******************************************************************************/
methodStatus sCpBackup(BackupConfig& config, string backupFilename, string subDir, string sCpParams) {
    string sCpBinary = locateBinary("scp");
    timer sCpTime;

    // superfluous check as test mode bombs out of performBackup() long before it ever calls sCpBackup().
    // but just in case future logic changes, testing here
    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW << config.ifTitle() + " TESTMODE: would have SCPd " +  backupFilename +
            " to " << sCpParams << RESET << endl;
        return methodStatus(true, "");
    }

    if (!sCpBinary.length()) {
        SCREENERR("\t• SCP skipped (unable to locate 'scp' binary in the PATH)");
        return methodStatus(false, "\t• SCP: unable to locate 'scp' binary in the PATH");
    }

    string screenMessage = config.ifTitle() + " SCPing to " + sCpParams + "... ";
    string backspaces = string(screenMessage.length(), '\b');
    string blankspaces = string(screenMessage.length() , ' ');
    NOTQUIET && cout << screenMessage << flush;

    // execute the scp
    sCpTime.start();
    int result = system(string(sCpBinary + " " + backupFilename + " " + sCpParams).c_str());
    
    sCpTime.stop();
    NOTQUIET && cout << backspaces << blankspaces << backspaces << flush;

    if (result == -1 || result == 127) {
        log(config.ifTitle() + " error executing " + sCpBinary);
        SCREENERR("\t• SCP failed for " << backupFilename << " to " << sCpParams);
        return methodStatus(false, "\t• SCP: error executing " + sCpBinary);
    }
    else {
        log(config.ifTitle() + " " + backupFilename + " scp'd to " + sCpParams + " in " + sCpTime.elapsed());
        NOTQUIET && cout << "\t• SCPd " << backupFilename << " to " << sCpParams << " in " << sCpTime.elapsed() << endl;
        return methodStatus(true, "\t• SCPd " + backupFilename + " to " + sCpParams + " in " + sCpTime.elapsed());
    }
}


/*******************************************************************************
 * sFtpBackup(config, backupFilename, subDir, sFtpParams)
 *
 * SFTP the backup to the specified destination.
 *******************************************************************************/
methodStatus sFtpBackup(BackupConfig& config, string backupFilename, string subDir, string sFtpParams) {
    string sFtpBinary = locateBinary("sftp");
    bool makeDirs = sFtpParams.find("//") == string::npos;
    strReplaceAll(sFtpParams, "//", "/");
    PipeExec sFtp(sFtpBinary + " " + sFtpParams);
    string command;
    timer sFtpTime;

    // superfluous check as test mode bombs out of performBackup() long before it ever calls sFtpBackup().
    // but just in case future logic changes, testing here
    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW << config.ifTitle() + " TESTMODE: would have SFTPd via '" + sFtpParams + 
            "' and uploaded " + subDir + "/" + backupFilename << RESET << endl;
        return methodStatus(true, "");
    }

    if (!sFtpBinary.length()) {
        SCREENERR("\t• SFTP skipped (unable to locate 'sftp' binary in the PATH");
        return methodStatus(false, "\t• SFTP: unable to locate 'sftp' binary in the PATH");
    }

    string screenMessage = config.ifTitle() + " SFTPing via " + sFtpParams + "... ";
    string backspaces = string(screenMessage.length(), '\b');
    string blankspaces = string(screenMessage.length() , ' ');
    NOTQUIET && cout << screenMessage << flush;

    // execute the sftp command
    sFtpTime.start();
    sFtp.execute(config.settings[sTitle].value);

    if (makeDirs) {
        char data[1500];
        strcpy(data, subDir.c_str());
        char *p = strtok(data, "/");
        string path;

        // make each component of the subdirectory "mkdir -p" style
        while (p) {
            path += string("/") + p;
            command = "mkdir " + path + "\n";
            sFtp.writeProc(command.c_str(), command.length());
            p = strtok(NULL, "/");
        }

        // cd to the new subdirectory
        command = "cd " + subDir + "\n";
        sFtp.writeProc(command.c_str(), command.length());
    }

    // check disk space
    auto requiredSpace = approx2bytes(config.settings[sMinSFTPSpace].value);
    if (requiredSpace) {
        command = "df .\n";
        sFtp.writeProc(command.c_str(), command.length());
        auto freeSpace = sFtp.statefulReadAndMatchRegex("Avail.*%Capacity\n\\s*\\d+\\s+\\d+\\s+(\\d+)\\s+\\d+\\s+\\d+%");

        if (freeSpace.length()) {
            auto availSpace = approx2bytes(freeSpace + "K");

            if (availSpace < requiredSpace) {
                NOTQUIET && cout << backspaces << blankspaces << backspaces << flush;
                string msg = " SFTP aborted due to insufficient disk space (" + approximate(availSpace) + ") on the remote server, " +
                        approximate(requiredSpace) + " required";
                log(config.ifTitle() + msg);
                SCREENERR("\t•" + msg);
                return methodStatus(false, "\t•" + msg);
            }
            else 
                DEBUG(2, "\nSFTP space check passed (avail " << approximate(availSpace) << ", required " << approximate(requiredSpace) << ")");
        }
        else
            log(config.ifTitle() + " unable to check free space (df) on the SFTP server; continuing");
    }

    // upload the backup file
    command = "put " + backupFilename + "\n";
    sFtp.writeProc(command.c_str(), command.length());

    command = string("quit\n");
    sFtp.writeProc(command.c_str(), command.length());

    bool success = sFtp.readAndMatch("Uploading");
    sFtp.closeAll();
    sFtpTime.stop();
    NOTQUIET && cout << backspaces << blankspaces << backspaces << flush;

    if (success) {
        NOTQUIET && cout << "\t• SFTPd " << backupFilename << " via " << sFtpParams << " in " << sFtpTime.elapsed() << endl;
        log(config.ifTitle() + " " + backupFilename + " sftp'd via " + sFtpParams + " in " + sFtpTime.elapsed());
        return methodStatus(true, "\t• SFTPd " + backupFilename + " via " + sFtpParams + " in " + sFtpTime.elapsed());
    }
    else {
        SCREENERR("\t• SFTP failed for " << backupFilename << " via " << sFtpParams);
        log(config.ifTitle() + " failed to sftp " + backupFilename + " via " + sFtpParams + "; see " + string(TMP_OUTPUT_DIR));
        return methodStatus(false, "\t• SFTP failed for " + backupFilename + " via " + sFtpParams + ", see " + string(TMP_OUTPUT_DIR));
    }
}


/*******************************************************************************
 * performBackup(config)
 *
 * Perform a backup, saving it to the configured filename.
 *******************************************************************************/
void performBackup(BackupConfig& config) {
    bool incTime = str2bool(config.settings[sIncTime].value);
    string setFname = config.settings[sBackupFilename].value;
    string setDir = config.settings[sDirectory].value;
    string setCommand = config.settings[sBackupCommand].value;
    string tempExtension = ".tmp." + to_string(GLOBALS.pid);

    if (!setFname.length() || GLOBALS.cli.count(CLI_NOBACKUP) || !setCommand.length())
        return;

    // setup path names and filenames
    time_t now;
    char buffer[100];
    now = time(NULL);

    strftime(buffer, sizeof(buffer), incTime ? "%Y/%m/%d": "%Y/%m", localtime(&now));
    string subDir = buffer;

    strftime(buffer, sizeof(buffer), incTime ? "-%Y%m%d-%H:%M:%S": "-%Y%m%d", localtime(&now));
    string fnameInsert = buffer;

    string fullDirectory = addSlash(setDir) + subDir + "/";
    string basicFilename;

    Pcre fnamePartsRE("(.*)(\\.[^.]+)$");
    if (fnamePartsRE.search(setFname) && fnamePartsRE.matches() > 1) 
        basicFilename = fnamePartsRE.get_match(0) + fnameInsert + fnamePartsRE.get_match(1);
    else
        basicFilename = setFname + fnameInsert;
    string backupFilename = fullDirectory + basicFilename;

    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW << config.ifTitle() << " TESTMODE: would have begun backup to " <<
            backupFilename << RESET << endl;
        return;
    }

    // make sure the destination directory exists
    mkdirp(fullDirectory);

    log(config.ifTitle() + " starting backup to " + backupFilename);
    string screenMessage = config.ifTitle() + " backing up to temp file " + backupFilename + tempExtension + "... ";
    string backspaces = string(screenMessage.length(), '\b');
    string blankspaces = string(screenMessage.length() , ' ');
    NOTQUIET && cout << screenMessage << flush;

    // note start time
    timer backupTime;
    backupTime.start();

    // begin backing up
    PipeExec proc(setCommand);
    GLOBALS.interruptFilename = backupFilename + tempExtension;
    proc.execute2file(GLOBALS.interruptFilename, safeFilename(config.settings[sTitle].value));
    GLOBALS.interruptFilename = "";  // interruptFilename gets cleaned up on SIGINT & SIGTERM

    // note finish time
    backupTime.stop();
    NOTQUIET && cout << backspaces << blankspaces << backspaces << flush;

    // determine results
    struct stat statData;
    if (!stat(string(backupFilename + tempExtension).c_str(), &statData)) {
        if (statData.st_size >= approx2bytes(config.settings[sMinSize].value)) {

            // calculate the md5 while its still a temp file so that its ignored by other
            // invocations of managebackups (i.e. someone running -0 while a backup is still
            // running in the background). then rename the file when we're all done.
            BackupEntry cacheEntry;
            cacheEntry.filename = backupFilename + tempExtension;
            cacheEntry.links = statData.st_nlink;
            cacheEntry.mtime = statData.st_mtime;
            cacheEntry.inode = statData.st_ino;
            cacheEntry.size = statData.st_size;
            cacheEntry.duration = backupTime.seconds();
            cacheEntry.updateAges(backupTime.endTime.tv_sec);
            cacheEntry.calculateMD5();

            // rename the file
            if (!rename(string(backupFilename + tempExtension).c_str(), backupFilename.c_str())) {
                auto size = approximate(cacheEntry.size);

                log(config.ifTitle() + " completed backup to " + backupFilename + " (" + size + ") in " + backupTime.elapsed());
                NOTQUIET && cout << "\t• successfully backed up to " << BOLDBLUE << backupFilename << RESET <<
                    " (" << size << ") in " << backupTime.elapsed() << endl;

                try {   // could get an exception converting settings[sMode] to an octal number
                    int mode = strtol(config.settings[sMode].value.c_str(), NULL, 8);

                    if (chmod(backupFilename.c_str(), mode)) {
                        SCREENERR("error: unable to chmod " << config.settings[sMode].value << " on " << backupFilename);
                        log("error: unable to chmod " + config.settings[sMode].value + " on " + backupFilename);
                    }
                }
                catch (...) {
                    string err = string("error: invalid file mode specified (") + config.settings[sMode].value.c_str() + ")";
                    SCREENERR(err);
                    log(err);
                }

                cacheEntry.filename = backupFilename;         // rename the file in the cache entry
                config.cache.addOrUpdate(cacheEntry, true, true);
                config.cache.reStatMD5(cacheEntry.md5);

                // let's commit the cache to disk now instead of via the destructor so that anyone running
                // a -0 or -1 while we're still finishing our SCP/SFTP in the background won't have to recalculate
                // the MD5 of the backup we just took.
                config.cache.saveCache();
                config.cache.newMD5 = false;    // this disables the now redundant save in the destructor

                bool overallSuccess = true;
                string notifyMessage = "\t• completed backup of " + backupFilename + " (" + size + ") in " + backupTime.elapsed() + "\n";

                if (config.settings[sSFTPTo].value.length()) {
                    string sFtpParams = interpolate(config.settings[sSFTPTo].value, subDir, fullDirectory, basicFilename);
                    auto sFTPStatus = sFtpBackup(config, backupFilename, subDir, sFtpParams);
                    overallSuccess &= sFTPStatus.success;
                    notifyMessage += "\n" + sFTPStatus.detail + "\n";
                }

                if (config.settings[sSCPTo].value.length()) {
                    string sCpParams = interpolate(config.settings[sSCPTo].value, subDir, fullDirectory, basicFilename);
                    auto sCPStatus = sCpBackup(config, backupFilename, subDir, sCpParams);
                    overallSuccess &= sCPStatus.success;
                    notifyMessage += "\n" + sCPStatus.detail + "\n";
                }

                notify(config, notifyMessage, overallSuccess);
            }
            else {
                log(config.ifTitle() + " backup failed, unable to rename temp file to " + backupFilename);
                unlink(string(backupFilename + tempExtension).c_str());
                notify(config, "Failed to backup to " + backupFilename + "\nUnable to rename temp file.\n", false);
                SCREENERR("\t• backup failed to " << backupFilename);
            }
        }
        else {
            log(config.ifTitle() + " backup failed to " + backupFilename + " (insufficient output/size)");
            notify(config, "Failed to backup to " + backupFilename + "\nInsufficient output.\n", false);
            SCREENERR("\t• backup failed to " << backupFilename << " (insufficient output/size)");
        }
    }
    else {
        log(config.ifTitle() + " backup command failed to generate any output");
        notify(config, "Failed to backup to " + backupFilename + "\nNo output generated by backup command.\n", false);
        SCREENERR("\t• backup failed to generate any output");
    }
}


/*******************************************************************************
 * setupUserDirectories()
 *
 * Configure the 3 primary app directories (config, cache, log) to be relative
 * to the current user's home directory.
 *******************************************************************************/
void setupUserDirectories() {
    struct passwd *pws;
    if ((pws = getpwuid(getuid())) == NULL) {
        SCREENERR("error: unable to lookup current user");
        exit(1);
    }
    
    string mbs = "managebackups";
    GLOBALS.confDir = addSlash(pws->pw_dir) + mbs + "/etc";
    GLOBALS.cacheDir = addSlash(pws->pw_dir) + mbs + "/var/cache";
    GLOBALS.logDir = addSlash(pws->pw_dir) + mbs + "/var/log";
}


/*******************************************************************************
 * sigTermHandler(sig)
 *
 * Catch the configured signals and clean up any inprocess backup.
 *******************************************************************************/
void sigTermHandler(int sig) {
    if (GLOBALS.interruptFilename.length()) {
        cerr << "\ninterrupt: aborting backup, cleaning up... ";
        unlink(GLOBALS.interruptFilename.c_str());
        cerr << "done." << endl;
    }

    log("error: operation aborted on signal " + to_string(sig));
    exit(1);
}


bool enoughLocalSpace(BackupConfig& config) {
    auto requiredSpace = approx2bytes(config.settings[sMinSpace].value);

    DEBUG(1, "required for backup: " << requiredSpace);
    if (!requiredSpace)
        return true;

    struct statfs fs;
    if (!statfs(config.settings[sDirectory].value.c_str(), &fs)) {
        auto availableSpace = fs.f_bsize * fs.f_bavail;

        DEBUG(1, config.settings[sDirectory].value << ": available=" << availableSpace << " (" << approximate(availableSpace) << "), required=" << requiredSpace
                << " (" << approximate(requiredSpace) << ")");

        if (availableSpace < requiredSpace) {
            string msg = "error: insufficient space (" + approximate(availableSpace) + ") to start a new backup; " + 
                approximate(requiredSpace) + " required.";
            notify(config, msg, true);
            log(config.ifTitle() + " " + msg);
            SCREENERR(msg);
            return false;
        }
    }
    else {
        log("error: unable to statvfs() the filesystem that " + config.settings[sDirectory].value + " is on (errno " + to_string(errno) + ")");
        SCREENERR("error: unable to statvfs() the filesystem that " << config.settings[sDirectory].value << " is on (errno " << errno << ")");
    }

    return true;
}


/*******************************************************************************
 * main(argc, argv)
 *
 * Main entry point -- where all the magic happens.
 *******************************************************************************/
int main(int argc, char *argv[]) {
    timer AppTimer;
    AppTimer.start();

    signal(SIGTERM, sigTermHandler);
    signal(SIGINT, sigTermHandler);

    GLOBALS.saveErrorSeen = false;
    GLOBALS.statsCount = 0;
    GLOBALS.md5Count = 0;
    GLOBALS.pid = getpid();

    // default directories
    GLOBALS.confDir = CONF_DIR;
    GLOBALS.cacheDir = CACHE_DIR;

    // overwrite with env vars (if any)
    string temp;
    temp = cppgetenv("MB_CONFDIR");
    if (temp.length())
        GLOBALS.confDir = temp;

    temp = cppgetenv("MB_CACHEDIR");
    if (temp.length())
        GLOBALS.cacheDir = temp;

    temp = cppgetenv("MB_LOGDIR");
    if (temp.length())
        GLOBALS.logDir = temp;

    time(&GLOBALS.startupTime);
    openlog("managebackups", LOG_PID | LOG_NDELAY, LOG_LOCAL1);
    cxxopts::Options options("managebackups", "Create and manage backups");

    options.add_options()
        (string("p,") + CLI_PROFILE, "Profile", cxxopts::value<std::string>())
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
        (string("u,") + CLI_USER, "User", cxxopts::value<bool>()->default_value("false"))
        (string("a,") + CLI_ALL, "All", cxxopts::value<bool>()->default_value("false"))
        (CLI_NOS, "Notify on success", cxxopts::value<bool>()->default_value("false"))
        (CLI_SAVE, "Save config", cxxopts::value<bool>()->default_value("false"))
        (CLI_FS_BACKUPS, "Failsafe Backups", cxxopts::value<int>())
        (CLI_FS_DAYS, "Failsafe Days", cxxopts::value<int>())
        (CLI_FS_FP, "Failsafe Paranoid", cxxopts::value<bool>()->default_value("false"))
        (CLI_DIR, "Directory", cxxopts::value<std::string>())
        (CLI_SCPTO, "SCP to", cxxopts::value<std::string>())
        (CLI_SFTPTO, "SFTP to", cxxopts::value<std::string>())
        (CLI_STATS1, "Stats summary", cxxopts::value<bool>()->default_value("false"))
        (CLI_STATS2, "Stats detail", cxxopts::value<bool>()->default_value("false"))
        (CLI_PRUNE, "Enable pruning", cxxopts::value<bool>()->default_value("false"))
        (CLI_NOPRUNE, "Disable pruning", cxxopts::value<bool>()->default_value("false"))
        (CLI_TEST, "Test only mode", cxxopts::value<bool>()->default_value("false"))
        (CLI_DEFAULTS, "Show defaults", cxxopts::value<bool>()->default_value("false"))
        (CLI_TIME, "Include time", cxxopts::value<bool>()->default_value("false"))
        (CLI_NOBACKUP, "Don't backup", cxxopts::value<bool>()->default_value("false"))
        (CLI_NOCOLOR, "Disable color", cxxopts::value<bool>()->default_value("false"))
        (CLI_HELP, "Show help", cxxopts::value<bool>()->default_value("false"))
        (CLI_CONFDIR, "Configuration directory", cxxopts::value<std::string>())
        (CLI_CACHEDIR, "Cache directory", cxxopts::value<std::string>())
        (CLI_LOGDIR, "Log directory", cxxopts::value<std::string>())
        (CLI_DOW, "Day of week for weeklies", cxxopts::value<int>())
        (CLI_VERSION, "Version", cxxopts::value<bool>()->default_value("false"))
        (CLI_MODE, "File mode", cxxopts::value<std::string>())
        (CLI_MINSPACE, "Minimum local space", cxxopts::value<std::string>())
        (CLI_MINSFTPSPACE, "Minimum SFTP space", cxxopts::value<std::string>())
        (CLI_INSTALLMAN, "Install man", cxxopts::value<bool>()->default_value("false"))
        (CLI_INSTALL, "Install", cxxopts::value<bool>()->default_value("false"));

    try {
        GLOBALS.cli = options.parse(argc, argv);
        GLOBALS.debugLevel = GLOBALS.cli.count(CLI_VERBOSE);
        GLOBALS.color = !(GLOBALS.cli[CLI_QUIET].as<bool>() || GLOBALS.cli[CLI_NOCOLOR].as<bool>());
        GLOBALS.stats = GLOBALS.cli.count(CLI_STATS1) || GLOBALS.cli.count(CLI_STATS2);

        if (GLOBALS.cli.count(CLI_USER)) 
            setupUserDirectories();

        if (GLOBALS.cli.count(CLI_CONFDIR))
            GLOBALS.confDir = GLOBALS.cli[CLI_CONFDIR].as<string>();

        if (GLOBALS.cli.count(CLI_CACHEDIR))
            GLOBALS.cacheDir = GLOBALS.cli[CLI_CACHEDIR].as<string>();

        if (GLOBALS.cli.count(CLI_LOGDIR))
            GLOBALS.logDir = GLOBALS.cli[CLI_LOGDIR].as<string>();
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

    if (GLOBALS.cli.count(CLI_HELP)) {
        showHelp(hOptions);
        exit(0);
    }
    
    if (GLOBALS.cli.count(CLI_INSTALLMAN)) {
        installman();
        exit(0);
    }

    if (GLOBALS.cli.count(CLI_INSTALL)) {
        install(argv[0]);
        exit(0);
    }

    if (GLOBALS.cli.count(CLI_VERSION)) {
        cout << "managebackups " << VERSION << endl;
        exit(0);
    }

    if (GLOBALS.cli.count(CLI_ALL) && GLOBALS.cli.count(CLI_PROFILE)) {
        SCREENERR("error: all and profile are mutually-exclusive options");
        exit(1);
    }

    DEBUG(2, "about to setup config...");
    ConfigManager configManager;
    auto currentConfig = selectOrSetupConfig(configManager);

    // if displaying stats and --profile hasn't been specified (or matched successfully)
    // then rescan all configs;  otherwise just scan the --profile config
    DEBUG(2, "about to scan directories...");

    /* SHOW STATS
     * ****************************/
    if (GLOBALS.stats) {
        if (!currentConfig->temp)
            scanConfigToCache(*currentConfig);
        else
            for (auto &config: configManager.configs) 
                if (!config.temp)
                    scanConfigToCache(config);

        GLOBALS.cli.count(CLI_STATS1) ? displayDetailedStatsWrapper(configManager) : displaySummaryStatsWrapper(configManager);
    }
    else {
        /* ALL RUN (prune, link, backup)
         * for all profiles
         * ****************************/
        if (GLOBALS.cli.count(CLI_ALL)) {
            for (auto &config: configManager.configs) {
                if (!config.temp) {
                    scanConfigToCache(config);
                    NOTQUIET && cout << BOLDBLUE << "[" << config.settings[sTitle].value << "]" << RESET << "\n";
                    pruneBackups(config);
                    updateLinks(config);
                    if (enoughLocalSpace(config)) {
                        performBackup(config);
                    }
                }
            }
        }
        /* NORMAL RUN (prune, link, backup)
         * ****************************/
        else {
            scanConfigToCache(*currentConfig);
            pruneBackups(*currentConfig);
            updateLinks(*currentConfig);
            if (enoughLocalSpace(*currentConfig)) {
                performBackup(*currentConfig);
            }
        }

        DEBUG(2, "completed primary tasks");
    }
    
    AppTimer.stop();
    DEBUG(1, "stats: " << GLOBALS.statsCount << ", md5s: " << GLOBALS.md5Count << ", total time: " << AppTimer.elapsed(3));
    return 0;
}

