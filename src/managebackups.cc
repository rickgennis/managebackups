
#include "syslog.h"
#include "unistd.h"
#include <sys/stat.h>
#include <stdlib.h>
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


void parseDirToCache(string directory, string fnamePattern, BackupCache& cache) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    Pcre fnameRE(fnamePattern);
    Pcre tempRE("\\.tmp\\.\\d+$");
    bool testMode = GLOBALS.cli.count(CLI_TEST);

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
    string profile;
    BackupConfig tempConfig(true);
    BackupConfig* currentConf = &tempConfig; 
    bool bSave = GLOBALS.cli.count(CLI_SAVE);
    bool bTitle = GLOBALS.cli.count(CLI_PROFILE);

    // if --profile is specified on the command line set the active config
    if (bTitle) {
        if (int configNumber = configManager.config(GLOBALS.cli[CLI_PROFILE].as<string>())) {
            configManager.activeConfig = configNumber - 1;
            currentConf = &configManager.configs[configManager.activeConfig];
        }
        else if (!bSave && GLOBALS.stats) {
            SCREENERR("error: profile not found; try -1 or -2 with no profile to see all backups");
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

        if (GLOBALS.stats)
            configManager.loadAllConfigCaches();
        else
            currentConf->loadConfigsCache();
    }

    // if any other settings are given on the command line, incorporate them into the selected config.
    // that config will be the one found from --profile above (if any), or a temp config comprised only of defaults
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
        if (bTitle && bSave) {
            tempConfig.config_filename = addSlash(GLOBALS.confDir) + safeFilename(tempConfig.settings[sTitle].value) + ".conf";
            tempConfig.temp = false;
        }

        // if we're using a new/temp config, insert it into the list for configManager
        configManager.configs.insert(configManager.configs.end(), tempConfig);
        configManager.activeConfig = configManager.configs.size() - 1;
        currentConf = &configManager.configs[configManager.activeConfig];
    }

    if (!GLOBALS.stats && !currentConf->settings[sDirectory].value.length()) {
        SCREENERR("error: --directory is required");
        exit(1);
    }

    return(currentConf);
}




void pruneBackups(BackupConfig& config) {
    if (GLOBALS.cli.count(CLI_NOPRUNE))
        return;

    if (!config.settings[sPruneLive].value.length() && !GLOBALS.cli.count(CLI_NOPRUNE) && !GLOBALS.cli.count(CLI_QUIET)) {
        SCREENERR("warning: While a core feature, managebackups doesn't prune old backups" 
            << "until specifically enabled.  Use --prune to enable pruning.  Use --prune"
            << "and --save to make it the default behavior for this backup configuration."
            << "pruning skipped;  would have used these settings:"
            << "\t--days " << config.settings[sDays].value << " --weeks " << config.settings[sWeeks].value
            << " --months " << config.settings[sMonths].value << " --years " << config.settings[sYears].value);
        return;
    }

    // failsafe checks
    int fb = config.settings[sFailsafeBackups].ivalue();
    int fd = config.settings[sFailsafeDays].ivalue();
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
            string message = "skipping pruning due to failsafe check; only " + to_string(minValidBackups) +
                    " backup" + s(minValidBackups) + " within the last " + to_string(fd) + " day" + s(fd);
            
            SCREENERR("warning: " << message);
            log(config.ifTitle() + " " + message);
            return;
        }
    }

    set<string> changedMD5s;
    DEBUG(4, "weeklies set to dow " << config.settings[sDOW].ivalue());

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
            if (filenameAge / 7 <= config.settings[sWeeks].ivalue() && filenameDOW == config.settings[sDOW].ivalue()) {
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
            if (yearLimit && filenameAge / 365 <= yearLimit && raw_it->second.date_month == 1 && raw_it->second.date_day == 1) {
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
                    NOTQUIET && cout << "removed " << raw_it->second.filename << endl; 
                    log(config.ifTitle() + " removing " + raw_it->second.filename + 
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
                        cout << YELLOW << config.ifTitle() << " TESTMODE: would have linked " << detail << RESET << endl;
                    else {
                        if (!unlink(raw_it->second.filename.c_str())) {
                            if (!link(referenceFile->filename.c_str(), raw_it->second.filename.c_str())) {
                                cout << "linked " << detail << endl;
                                log(config.ifTitle() + " linked " + detail);

                                config.cache.reStatMD5(md5_it->first);

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
                            SCREENERR("error: unable to remove" << raw_it->second.filename << " in prep to link it (" 
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
 

methodStatus sCpBackup(BackupConfig& config, string backupFilename, string subDir, string sCpParams) {
    string sCpBinary = locateBinary("scp");
    timer sCpTime;

    // superfluous check as test mode bombs out of performBackup() long before it ever calls sCpBackup().
    // but just in case future logic changes, testing here
    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW << config.ifTitle() + " TESTMODE: would have SCP'd " +  backupFilename +
            " to " << sCpParams << RESET << endl;
        return methodStatus(true, "");
    }

    if (!sCpBinary.length()) {
        SCREENERR("\t• SCP skipped (unable to locate 'scp' binary in the PATH)");
        return methodStatus(false, "\t• SCP: unable to locate 'scp' binary in the PATH");
    }

    // execute the scp
    sCpTime.start();
    int result = system(string(sCpBinary + " " + backupFilename + " " + sCpParams).c_str());
    sCpTime.stop();

    if (result == -1 || result == 127) {
        log(config.ifTitle() + " error executing " + sCpBinary);
        SCREENERR("\t• SCP failed for " << backupFilename << " to " << sCpParams);
        return methodStatus(false, "\t• SCP: error executing " + sCpBinary);
    }
    else {
        log(config.ifTitle() + " " + backupFilename + " scp'd to " + sCpParams + " in " + sCpTime.elapsed());
        NOTQUIET && cout << "\t• SCP'd " << backupFilename << " to " << sCpParams << " in " << sCpTime.elapsed() << endl;
        return methodStatus(true, "\t• SCP'd " + backupFilename + " to " + sCpParams + " in " + sCpTime.elapsed());
    }
}


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
        cout << YELLOW << config.ifTitle() + " TESTMODE: would have SFTP'd via '" + sFtpParams + 
            "' and uploaded " + subDir + "/" + backupFilename << RESET << endl;
        return methodStatus(true, "");
    }

    if (!sFtpBinary.length()) {
        SCREENERR("\t• SFTP skipped (unable to locate 'sftp' binary in the PATH");
        return methodStatus(false, "\t• SFTP: unable to locate 'sftp' binary in the PATH");
    }

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

    // upload the backup file
    command = "put " + backupFilename + "\n";
    sFtp.writeProc(command.c_str(), command.length());

    command = string("quit") + "\n";
    sFtp.writeProc(command.c_str(), command.length());

    bool success = sFtp.readAndMatch("Uploading");
    sFtp.closeAll();
    sFtpTime.stop();

    if (success) {
        log(config.ifTitle() + " " + backupFilename + " sftp'd via " + sFtpParams + " in " + sFtpTime.elapsed());
        NOTQUIET && cout << "\t• SFTP'd " << backupFilename << " via " << sFtpParams << " in " << sFtpTime.elapsed() << endl;
        return methodStatus(true, "\t• SFTP'd " + backupFilename + " via " + sFtpParams + " in " + sFtpTime.elapsed());
    }
    else {
        log(config.ifTitle() + " failed to sftp " + backupFilename + " via " + sFtpParams + "; see " + string(TMP_OUTPUT_DIR));
        SCREENERR("\t• SFTP failed for " << backupFilename << " via " << sFtpParams);
        return methodStatus(false, "\t• SFTP failed for " + backupFilename + " via " + sFtpParams + "; see " + string(TMP_OUTPUT_DIR));
    }
}


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
    proc.execute2file(backupFilename + tempExtension, safeFilename(config.settings[sTitle].value));

    // note finish time
    backupTime.stop();
    NOTQUIET && cout << backspaces << blankspaces << backspaces << flush;

    // determine results
    struct stat statData;
    if (!stat(string(backupFilename + tempExtension).c_str(), &statData)) {
        if (statData.st_size >= config.settings[sMinSize].ivalue()) {
            if (!rename(string(backupFilename + tempExtension).c_str(), backupFilename.c_str())) {
                log(config.ifTitle() + " completed backup to " + backupFilename + " in " + backupTime.elapsed());
                NOTQUIET && cout << "\t• successfully backed up to " << BOLDBLUE << backupFilename << RESET <<
                    " in " << backupTime.elapsed() << endl;

                BackupEntry cacheEntry;
                cacheEntry.filename = backupFilename;
                cacheEntry.links = statData.st_nlink;
                cacheEntry.mtime = statData.st_mtime;
                cacheEntry.inode = statData.st_ino;
                cacheEntry.size = statData.st_size;
                cacheEntry.duration = backupTime.seconds();
                cacheEntry.updateAges(backupTime.endTime.tv_sec);
                cacheEntry.calculateMD5();

                config.cache.addOrUpdate(cacheEntry, true);
                config.cache.reStatMD5(cacheEntry.md5);

                bool overallSuccess = true;
                string notifyMessage = config.ifTitle() + "\n\t• completed backup of " + backupFilename + " in " + backupTime.elapsed() + "\n\n";

                if (config.settings[sSFTPTo].value.length()) {
                    string sFtpParams = interpolate(config.settings[sSFTPTo].value, subDir, fullDirectory, basicFilename);
                    auto sFTPStatus = sFtpBackup(config, backupFilename, subDir, sFtpParams);
                    overallSuccess &= sFTPStatus.success;
                    notifyMessage += sFTPStatus.detail + "\n";
                }

                if (config.settings[sSCPTo].value.length()) {
                    string sCpParams = interpolate(config.settings[sSCPTo].value, subDir, fullDirectory, basicFilename);
                    auto sCPStatus = sCpBackup(config, backupFilename, subDir, sCpParams);
                    overallSuccess &= sCPStatus.success;
                    notifyMessage += sCPStatus.detail + "\n";
                }

                notify(config, notifyMessage, overallSuccess);
            }
            else {
                log(config.ifTitle() + " backup failed, unable to rename temp file to " + backupFilename);
                unlink(string(backupFilename + tempExtension).c_str());
                notify(config, config.ifTitle() + "\nFailed to backup to " + backupFilename + "\nUnable to rename temp file.\n", false);
                SCREENERR("\t• backup failed to " << backupFilename);
            }
        }
        else {
            log(config.ifTitle() + " backup failed to " + backupFilename + " (insufficient output/size)");
            notify(config, config.ifTitle() + "\nFailed to backup to " + backupFilename + "\nInsufficient output.\n", false);
            SCREENERR("\t• backup failed to " << backupFilename << " (insufficient output/size)");
        }
    }
    else {
        log(config.ifTitle() + " backup command failed to generate any output");
        notify(config, config.ifTitle() + "\nFailed to backup to " + backupFilename + "\nNo output generated by backup command.\n", false);
        SCREENERR("\t• backup failed to generate any output");
    }
}


int main(int argc, char *argv[]) {
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
        (CLI_INSTALLMAN, "Install man", cxxopts::value<bool>()->default_value("false"))
        (CLI_INSTALL, "Install", cxxopts::value<bool>()->default_value("false"));

    try {
        GLOBALS.cli = options.parse(argc, argv);
        GLOBALS.debugLevel = GLOBALS.cli.count(CLI_VERBOSE);
        GLOBALS.color = !GLOBALS.cli[CLI_NOCOLOR].as<bool>();
        GLOBALS.stats = GLOBALS.cli.count(CLI_STATS1) || GLOBALS.cli.count(CLI_STATS2);

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

    ConfigManager configManager;
    auto currentConfig = selectOrSetupConfig(configManager);

    // if displaying stats and --profile hasn't been specified (or matched successfully)
    // then rescan all configs;  otherwise just scan the --profile config
    if (GLOBALS.stats && currentConfig->temp) {
        for (auto cfg_it = configManager.configs.begin(); cfg_it != configManager.configs.end(); ++cfg_it) {
            scanConfigToCache(*cfg_it);
            cfg_it->cache.saveCache();
        }
    }
    else
        scanConfigToCache(*currentConfig);

    if (GLOBALS.stats) {
        GLOBALS.cli.count(CLI_STATS1) ? displayStats(configManager) : display1LineStats(configManager);
        exit(0);
    }

    pruneBackups(*currentConfig);
    updateLinks(*currentConfig);
    performBackup(*currentConfig);

    DEBUG(1, "stats: " << GLOBALS.statsCount << ", md5s: " << GLOBALS.md5Count);

    return 0;
}
