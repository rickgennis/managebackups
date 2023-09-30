/*
 * Copyright (C) 2023 Rick Ennis
 * This file is part of managebackups.
 *
 * managebackups is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * managebackups is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with managebackups.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *  managebackups
 *  2022 - 2023  Rick Ennis
 *
 *  managebackups provides 3 primary functions.  Each function can be run individually
 *  or all together:
 *
 *  1. Take backups
 *
 *     A. Single-file backups:  tar/dump/cpio/etc backups can be taken of local
 *        or remote filesystems.  Backups are automatically cataloged and indexed.
 *
 *     B. Faub-style backups:  Fully filesystem (or dir) backups can be taken where
 *        the result is laid out on disk for easy inspection via standard linux tools.
 *
 *  2. Pruning
 *
 *     A retention policy can be configured and applied per backup setup (profile).
 *     The policy is specified as the number of daily, weekly, monthly and yearly
 *     backups to keep.  Backups aged out of the policy are deleted.
 *
 *  3. Hard links
 *
 *     Regardless of the type of backup, hard-links can replace identical copies of
 *     files so that additional backups of files that haven't changed don't take up
 *     any additional disk space.
 */

#include <dirent.h>
#include <pcre++.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include <filesystem>
#include <iostream>
#include <fstream>

#include "syslog.h"
#include "unistd.h"
#include <utime.h>

#ifdef __APPLE__
#include <sys/mount.h>
#include <sys/param.h>
#else
#include <sys/statfs.h>
#endif

#include "BackupCache.h"
#include "BackupConfig.h"
#include "BackupEntry.h"
#include "ConfigManager.h"
#include "FaubCache.h"
#include "colors.h"
#include "cxxopts.hpp"
#include "debug.h"
#include "exception.h"
#include "faub.h"
#include "globalsdef.h"
#include "help.h"
#include "notify.h"
#include "setup.h"
#include "statistics.h"
#include "util_generic.h"

using namespace pcrepp;
struct global_vars GLOBALS;

struct methodStatus {
    bool success;
    string detail;
    
    methodStatus() { success = true; }
    methodStatus(bool s, string d)
    {
        success = s;
        detail = d;
    }
};


bool haveProfile(ConfigManager *cmP = NULL) {
    return (cmP != NULL && cmP->activeConfig > -1 ? !cmP->configs[cmP->activeConfig].temp : GLOBALS.cli.count(CLI_PROFILE));
}


/*******************************************************************************
 * verifyTripwireParams(param)
 *
 * Verify that the tripwire string is a valid syntax of colon delimited filename
 * and MD5 pairs.
 *******************************************************************************/
void verifyTripwireParams(string param)
{
    if (param.length()) {
        auto tripPairs = perlSplit("\\s*,\\s*", param);
        
        for (string tripPair : tripPairs) {
            auto tripItem = perlSplit("\\s*:\\s*", tripPair);
            
            if ((tripItem.size() != 2) || !tripItem[0].length() || !tripItem[1].length()) {
                SCREENERR("--" << CLI_TRIPWIRE << " item '" << tripPair
                          << "' isn't a colon delimited filename and MD5. e.g.\n"
                          << "/etc/testfile: 341990f48d4466bb64a82bdca01ef128");
                exit(1);
            }
        }
    }
}


struct parseDirDataType {
    Pcre *tempRE;
    BackupCache *cache;
};


bool parseDirCallback(pdCallbackData &file) {
    parseDirDataType *data = (parseDirDataType*)file.dataPtr;
    
    // first test for an in-process file.  if its in process, is it old and abandoned?
    // if so, delete it.  if not old, note the in process filename and skip the rest
    auto ps = pathSplit(file.filename);
    if (data->tempRE->search(ps.file)) {
        DEBUG(D_scan) DFMT("in-process file found (" << file.filename << ")");
        
        if (GLOBALS.startupTime - file.statData.st_mtime > 3600 * 5) {
            DEBUG(D_scan) DFMT("removing abandoned in-process file (" << file.filename << ")");
            unlink(file.filename.c_str());
        }
        else
            data->cache->inProcess = file.filename;
    }
    else {
        auto shouldCalculateMD5 = true;
        
        // not an in-process file, let's make sure its in the cache.
        // if the cache has an existing md5 and the cache's mtime and size match
        // what we just read from disk, consider the cache valid.  only update
        // the inode & age info.
        string reason;
        BackupEntry *pCacheEntry;
        if ((pCacheEntry = data->cache->getByFilename(file.filename)) != NULL) {
            if (pCacheEntry->md5.length() && pCacheEntry->size == file.statData.st_size &&
                pCacheEntry->mtime && pCacheEntry->mtime == file.statData.st_mtime) {
                pCacheEntry->links = file.statData.st_nlink;
                pCacheEntry->inode = file.statData.st_ino;
                
                data->cache->addOrUpdate(*pCacheEntry->updateAges(GLOBALS.startupTime), true);
                shouldCalculateMD5 = false;
            }
            else {
                if (!pCacheEntry->md5.length()) { reason = "{no md5}"; }
                else
                    if (pCacheEntry->size != file.statData.st_size) { reason = "{size change}"; }
                    else
                        if (!pCacheEntry->mtime) { reason = "{no mtime}"; }
                        else
                            if (pCacheEntry->mtime != file.statData.st_mtime) { reason = "{mtime change}"; }
                            else
                                reason = "{?}";
            }
        }
        else
            reason = "{not in cache}";
        
        if (shouldCalculateMD5) {
            // otherwise let's update the cache with everything we just read and
            // then calculate a new md5
            BackupEntry cacheEntry;
            cacheEntry.filename = file.filename;
            cacheEntry.links = file.statData.st_nlink;
            cacheEntry.mtime = file.statData.st_mtime;
            cacheEntry.inode = file.statData.st_ino;
            cacheEntry.size = file.statData.st_size;
            cacheEntry.updateAges(GLOBALS.startupTime);
            
            if (cacheEntry.calculateMD5(reason)) {
                data->cache->addOrUpdate(cacheEntry, true, true);
            }
            else {
                log("error: unable to read " + file.filename + " (MD5)" + errtext());
                SCREENERR("error: unable to read " << file.filename << " (MD5)" + errtext());
            }
        }
    }
    
    return true;
}


/*******************************************************************************
 * parseDirToCache(directory, fnamePattern, cache)
 *
 * [for single-file backups only]
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
void parseDirToCache(string directory, string fnamePattern, BackupCache &cache) {
    Pcre tempRE("\\.tmp\\.\\d+$");
    parseDirDataType data;
    data.tempRE = &tempRE;
    data.cache = &cache;
    
    processDirectoryBackups(ue(directory), fnamePattern, false, parseDirCallback, &data, SINGLE_ONLY);
}



/*******************************************************************************
 * scanConfigToCache(config)
 *
 * Wrapper function for parseDirToCache().
 *******************************************************************************/
void scanConfigToCache(BackupConfig &config) {
    if (config.isFaub()) {
        config.fcache.restoreCache(config.settings[sDirectory].value, config.settings[sTitle].value, config.settings[sUUID].value);
        
        // clean up old cache files and recalculate any missing disk usage
        // this primarily catches recalculations that are necessary because
        // the user manually deleted a backup unbeknown to us.
        config.fcache.cleanup();
        return;
    }
    
    config.cache.cleanup();
    
    string fnamePattern = "";
    string directory = config.settings[sDirectory].value;
    
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
    
    if (!exists(directory)) {
        if (mkdirp(directory)) {
            SCREENERR("error: unable to create directory '" << directory << "': " << errtext());
            exit(1);
        }
    }
    
    parseDirToCache(directory, fnamePattern, config.cache);
}


void saveErrorAndExit() {
    SCREENERR(
              "error: --profile must be specified in order to --save settings.\n"
              << "Once saved --profile becomes a macro for all settings specified with the "
              "--save.\n"
              << "For example, these two commands would do the same things:\n\n"
              << "\tmanagebackups --profile myback --directory /etc --file etc.tgz --daily 5 "
              "--save\n"
              << "\tmanagebackups --profile myback\n\n"
              << "Options specified with a profile that's aleady been saved will override that\n"
              << "option for this run only (unless --save is given again).");
    exit(1);
}


/*******************************************************************************
 * selectOrSetupConfig(configManager)
 *
 * Determine which config/profile the user has selected and load it. If no
 * profile was selected created a temp one that will be used, but not persisted
 * to disk. Validate required commandline options.
 *******************************************************************************/
BackupConfig *selectOrSetupConfig(ConfigManager &configManager, bool allowDefaultProfile = false) {
    string profile;
    BackupConfig tempConfig(true);
    BackupConfig *currentConf = &tempConfig;
    bool bSave = GLOBALS.cli.count(CLI_SAVE);
    bool bProfile = GLOBALS.cli.count(CLI_PROFILE);
    
    // if --profile is specified on the command line set the active config
    if (bProfile) {
        int configNumber;
        if ((configNumber = configManager.findConfig(GLOBALS.cli[CLI_PROFILE].as<string>())) > 0) {
            configManager.activeConfig = configNumber - 1;
            currentConf = &configManager.configs[configManager.activeConfig];
        }
        else
            if (configNumber == -1) {
                SCREENERR("error: more than one profile matches selection.");
                exit(1);
            }
            else
                if (!bSave) {
                    SCREENERR("error: profile not found; try -0 or -1 with no profile to see all backups\n"
                              << "or use --save to create this profile.");
                    exit(1);
                }
        
        if (currentConf->settings[sTitle].value.length() && currentConf->settings[sTitle].value != GLOBALS.cli[CLI_PROFILE].as<string>() && bSave) {
            SCREENERR("error: the specified partial profile name matches an existing profile (" << currentConf->settings[sTitle].value
                      << ");\nprovide a unique name for a new profile or the exact name to update " << currentConf->settings[sTitle].value);
            exit(1);
        }
        
        currentConf->loadConfigsCache();
    }
    else
        if (allowDefaultProfile) {
            if (bSave)
                saveErrorAndExit();
            
            if (configManager.defaultConfig.length()) {
                configManager.activeConfig = configManager.findConfig(configManager.defaultConfig) - 1;
                currentConf = &configManager.configs[configManager.activeConfig];
                
            }
        }
        else {
            if (bSave)
                saveErrorAndExit();
            
            if (GLOBALS.stats || GLOBALS.cli.count(CLI_ALLSEQ) || GLOBALS.cli.count(CLI_CRONS))
                configManager.loadAllConfigCaches();
            else
                currentConf->loadConfigsCache();
        }
    
    // if any other settings are given on the command line, incorporate them into the selected
    // config. that config will be the one found from --profile above (if any), or a temp config
    // comprised only of defaults
    for (auto &setting : currentConf->settings)
        if (GLOBALS.cli.count(setting.display_name)) {
            DEBUG(D_config) DFMT("command line param: " << setting.display_name << " (type " << setting.data_type << ")");
            
            switch (setting.data_type) {
                case INT:
                    if (bSave &&
                        (setting.value != to_string(GLOBALS.cli[setting.display_name].as<int>())))
                        currentConf->modified = 1;
                    setting.value = to_string(GLOBALS.cli[setting.display_name].as<int>());
                    setting.execParam = "--" + setting.display_name + " " + setting.value;
                    break;
                    
                case STRING:
                default:
                    // the rest of this STRING block will update the value of Profile with anything
                    // specified after -p on the commandline.  we only require -p to be a precise name when
                    // creating a new profile (i.e. when --save is also given).  otherwise -p can be
                    // a partial string used to match an existing profile.  so if there's no --save
                    // and therefore we may have a partial match value given on the commandline,
                    // don't update the value of the profile name itself with the commandline specification.
                    // instead, break.
                    if ((setting.display_name == CLI_PROFILE) && !bSave)
                        break;
                    
                    // special-case for something that can look like a SIZE or be a percentage
                    if (setting.display_name == CLI_BLOAT) {
                        for (int i=0; i < setting.value.length(); ++i)
                            if (!isdigit(setting.value[i]) && setting.value[i] != '%') {
                                try {
                                    approx2bytes(setting.value);
                                }
                                catch (...) {
                                    log("error: invalid value specified for --" + setting.display_name +
                                        " (" + setting.value + ")");
                                    SCREENERR("error: invalid value specified for option"
                                              << "\n\t--" << setting.display_name << " " << setting.value << "\n" <<
                                              "value should be a size ('2G') or a percentage ('85%').");
                                    exit(1);
                                }
                                
                                break;
                            }
                    }
                    
                    if (bSave && (setting.value != GLOBALS.cli[setting.display_name].as<string>()))
                        currentConf->modified = 1;
                    setting.value = GLOBALS.cli[setting.display_name].as<string>();
                    setting.execParam = "--" + setting.display_name + " '" + setting.value + "'";
                    
                    if (setting.display_name == CLI_TRIPWIRE)
                        verifyTripwireParams(setting.value);
                    break;
                    
                case OCTAL:
                    if (bSave && (setting.value != GLOBALS.cli[setting.display_name].as<string>()))
                        currentConf->modified = 1;
                    try {
                        setting.value = GLOBALS.cli[setting.display_name].as<string>();
                        setting.execParam = "--" + setting.display_name + " " + setting.value;
                        stol(setting.value, NULL, 8);  // throws on error
                    }
                    catch (...) {
                        log("error: invalid octal value specified for --" + setting.display_name +
                            " (" + setting.value + ")");
                        SCREENERR("error: invalid octal value specified for option"
                                  << "\n\t--" << setting.display_name << " " << setting.value);
                        exit(1);
                    }
                    break;
                    
                case SIZE:
                    if (bSave && (setting.value != GLOBALS.cli[setting.display_name].as<string>()))
                        currentConf->modified = 1;
                    try {
                        setting.value = GLOBALS.cli[setting.display_name].as<string>();
                        setting.execParam = "--" + setting.display_name + " '" + setting.value + "'";
                        approx2bytes(setting.value);  // throws on error
                    }
                    catch (...) {
                        log("error: invalid size value specified for --" + setting.display_name +
                            " (" + setting.value + ")");
                        SCREENERR("error: invalid size value specified for option"
                                  << "\n\t--" << setting.display_name << " " << setting.value);
                        exit(1);
                    }
                    break;
                    
                case BOOL:
                    if (bSave &&
                        (setting.value != to_string(GLOBALS.cli[setting.display_name].as<bool>())))
                        currentConf->modified = 1;
                    setting.value = to_string(GLOBALS.cli[setting.display_name].as<bool>());
                    setting.execParam = "--" + setting.display_name;
                    break;
            }
        }
    
    if (GLOBALS.cli.count(CLI_RECREATE)) currentConf->modified = 1;
    
    // apply fp from the config file, if set
    if (str2bool(currentConf->settings[sFP].value)) {
        currentConf->settings[sFailsafeDays].value = "2";
        currentConf->settings[sFailsafeBackups].value = "1";
        currentConf->settings[sFailsafeSlow].value = "2";
    }
    
    // convert --fp (failsafe paranoid) to its separate settings
    if (GLOBALS.cli.count(CLI_FS_FP)) {
        if (GLOBALS.cli.count(CLI_FS_DAYS) || GLOBALS.cli.count(CLI_FS_BACKUPS) || GLOBALS.cli.count(CLI_FS_SLOW)) {
            SCREENERR("error: --" << CLI_FS_FP << " is mutually exclusive with --" << CLI_FS_DAYS << ", --" << CLI_FS_BACKUPS << ", --" << CLI_FS_SLOW);
            exit(1);
        }
        
        if (bSave)  // this needed here?
            currentConf->modified = 1;
        
        currentConf->settings[sFailsafeDays].value = "2";
        currentConf->settings[sFailsafeBackups].value = "1";
        currentConf->settings[sFailsafeSlow].value = "2";
    }
    
    if (currentConf == &tempConfig) {
        if (bProfile && bSave) {
            tempConfig.config_filename = slashConcat(GLOBALS.confDir, safeFilename(tempConfig.settings[sTitle].value)) + ".conf";
            tempConfig.temp = false;
        }
        
        // if we're using a new/temp config, insert it into the list for configManager
        configManager.configs.insert(configManager.configs.end(), tempConfig);
        configManager.activeConfig = (int)configManager.configs.size() - 1;
        currentConf = &configManager.configs[configManager.activeConfig];
    }
    
    // user doesn't want to show stats & this isn't a faub client run via --path
    if (!GLOBALS.stats && !GLOBALS.cli.count(CLI_PATHS) &&
        
        // & the current profile (either via -p or full config spelled out in CLI) doesn't have a
        // dir specified
        !currentConf->settings[sDirectory].value.length() &&
        
        // or relocate
        !GLOBALS.cli.count(CLI_RELOCATE) &&
        
        // or compare
        !GLOBALS.cli.count(CLI_COMPARE) && !GLOBALS.cli.count(CLI_COMPAREFILTER) &&
        !GLOBALS.cli.count(CLI_LAST) &&
        
        // & user hasn't selected --all/--All where there's a dir configured in at least one (first)
        // profile
        !((GLOBALS.cli.count(CLI_ALLSEQ) || GLOBALS.cli.count(CLI_ALLPAR) ||
           GLOBALS.cli.count(CLI_CRONS) || GLOBALS.cli.count(CLI_CRONP)) &&
          configManager.configs.size() &&
          configManager.configs[0].settings[sDirectory].value.length())) {
        SCREENERR("error: --directory is required, or a --profile previously saved with a directory");
        exit(1);
    }
    
    return (currentConf);
}

/*******************************************************************************
 * pruneShoudKeep(...)
 *
 * Determine if the current file should be kept for daily/weekly/monthly/yearly
 * criteria.
 *******************************************************************************/
string pruneShouldKeep(BackupConfig &config, string filename, int filenameAge, int filenameDOW,
                       int filenameDay, int filenameMonth, int filenameYear) {
    //  daily
    if (config.settings[sDays].ivalue() && filenameAge <= config.settings[sDays].ivalue())
        return string("keep_daily: ") + filename + " (age=" + to_string(filenameAge) +
        ", dow=" + dw(filenameDOW) + ")";
    
    // weekly
    if (config.settings[sWeeks].ivalue() && filenameAge / 7.0 <= config.settings[sWeeks].ivalue() &&
        filenameDOW == config.settings[sDOW].ivalue())
        return string("keep_weekly: ") + filename + " (age=" + to_string(filenameAge) +
        ", dow=" + dw(filenameDOW) + ")";
    
    // monthly
    struct tm *now = localtime(&GLOBALS.startupTime);
    auto monthLimit = config.settings[sMonths].ivalue();
    auto monthAge =
    (now->tm_year + 1900) * 12 + now->tm_mon + 1 - (filenameYear * 12 + filenameMonth);
    if (monthLimit && monthAge <= monthLimit && filenameDay == 1)
        return string("keep_monthly: ") + filename + " (month_age=" + to_string(monthAge) +
        ", dow=" + dw(filenameDOW) + ")";
    
    // yearly
    auto yearLimit = config.settings[sYears].ivalue();
    auto yearAge = now->tm_year + 1900 - filenameYear;
    if (yearLimit && yearAge <= yearLimit && filenameMonth == 1 && filenameDay == 1)
        return string("keep_yearly: ") + filename + " (year_age=" + to_string(yearAge) +
        ", dow=" + dw(filenameDOW) + ")";
    
    return "";
}

/*******************************************************************************
 * pruneFaubBackups(config)
 *
 * Apply the full rentetion policy for Faub configs.  Safety checks are already
 * handled in pruneBackups() which is what called us here.
 *******************************************************************************/
void pruneFaubBackups(BackupConfig &config) {
    DEBUG(D_prune) DFMT("weeklies set to dow " << dw(config.settings[sDOW].ivalue()));
    DEBUG(D_prune) DFMT("examining " << plural(config.fcache.getNumberOfBackups(), "backup") << " for "
                        << config.settings[sTitle].value);
    
    size_t backupAge = 0, backupCountOnDay = 0, backupsPruned = 0;
    auto fsSlowLimit = config.settings[sFailsafeSlow].ivalue();
        
    auto cacheEntryIt = config.fcache.getFirstBackup();
    while (cacheEntryIt != config.fcache.getEnd()) {
        
        if (fsSlowLimit && backupsPruned >= fsSlowLimit) {
            DEBUG(D_prune) DFMT("failsafe_limit reached; " << plural(fsSlowLimit, "backup") << " pruned, aborting further prunes");
            break;
        }
        
        auto dataOnlyDelete = false;
        auto mtimeDayAge = cacheEntryIt->second.mtimeDayAge;
        auto filenameDOW = cacheEntryIt->second.dow;
        auto filenameDayAge = cacheEntryIt->second.filenameDayAge();
        
        auto shouldKeep =
        pruneShouldKeep(config, cacheEntryIt->second.getDir(), mtimeDayAge, filenameDOW,
                        cacheEntryIt->second.startDay, cacheEntryIt->second.startMonth,
                        cacheEntryIt->second.startYear);

        // if no files were changed in this backup and --dataonly is elected, delete this backup
        if (!cacheEntryIt->second.modifiedFiles && !cacheEntryIt->second.ds.getSize() && str2bool(config.settings[sDataOnly].value)) {
            dataOnlyDelete = true;
            shouldKeep = "";
        }
        
        if (backupAge != filenameDayAge) {
            backupAge = filenameDayAge;
            backupCountOnDay = 1;
        }
        else
            backupCountOnDay += 1;
        
        // if standard retention pruning deletes this backup and there's only one other
        // backup on this day, we don't want consolidation to delete the other one
        if (!shouldKeep.length()) backupCountOnDay -= 1;
        
        auto shouldConsolidate = config.settings[sConsolidate].ivalue() && backupAge >= config.settings[sConsolidate].ivalue() && backupCountOnDay > 1;
        
        // should we keep this backup?
        if (shouldKeep.length() && !shouldConsolidate) {
            DEBUG(D_prune) DFMT(shouldKeep);
            ++cacheEntryIt;
            continue;
        }
        
        string deleteReason = shouldConsolidate ? " (consolidation) " : dataOnlyDelete ? " (dataonly) " : " ";
        
        if (GLOBALS.cli.count(CLI_TEST)) {
            cout << YELLOW << config.ifTitle() << " TESTMODE: would have deleted "
            << cacheEntryIt->second.getDir() << " (age=" + to_string(mtimeDayAge)
            << ", dow=" + dw(filenameDOW) << ")" << deleteReason << RESET << endl;
            ++cacheEntryIt;
        }
        else {
            auto [message, noMessage] = clearMessage("removing " + cacheEntryIt->first + "...");
            NOTQUIET && ANIMATE && cout << message << flush;
            
            if (rmrf(cacheEntryIt->second.getDir())) {
                NOTQUIET && ANIMATE && cout << noMessage << flush;
                
                NOTQUIET && cout << "\t• removed " << cacheEntryIt->second.getDir() << deleteReason << endl;
                log(config.ifTitle() + " removed " + cacheEntryIt->second.getDir() +
                    " (age=" + to_string(mtimeDayAge) + ", dow=" + dw(filenameDOW) + ")" + deleteReason);
                DEBUG(D_prune) DFMT("completed removal of " << cacheEntryIt->second.getDir() << deleteReason);
                ++backupsPruned;
            }
            else {
                NOTQUIET && ANIMATE && cout << noMessage << flush;
                
                log(config.ifTitle() + " unable to remove" + deleteReason + cacheEntryIt->second.getDir() + errtext());
                SCREENERR(string("unable to remove") + deleteReason + cacheEntryIt->second.getDir() + errtext());
            }
            
            auto deadBackupIt = cacheEntryIt++;
            config.fcache.removeBackup(deadBackupIt);
        }
    }
    
    /* if a faub backup is deleted we'll need to recalculate the disk usage of
     the next cronological backup in order (if any). cleanup() handles this. */
    config.fcache.cleanup();
}


bool shouldPrune(BackupConfig &config) {
    if (GLOBALS.cli.count(CLI_NOPRUNE))
        return false;
    
    if (!str2bool(config.settings[sPruneLive].value) && !GLOBALS.cli.count(CLI_QUIET)) {
        SCREENERR("warning: while a core feature, managebackups doesn't prune old backups\n"
                  << "until specifically enabled.  Use --prune to enable pruning.  Use --prune\n"
                  << "and --save to make it the default behavior for this profile.\n"
                  << "pruning skipped; would have used these settings:\n"
                  << "\t--days " << config.settings[sDays].value << "\n\t--weeks "
                  << config.settings[sWeeks].value << "\n\t--months " << config.settings[sMonths].value
                  << "\n\t--years " << config.settings[sYears].value);
        return false;
    }
    
    // failsafe checks
    int fb = config.settings[sFailsafeBackups].ivalue();
    int fd = config.settings[sFailsafeDays].ivalue();
    
    string descrip;
    if (fb > 0 && fd > 0) {
        int minValidBackups = 0;
        
        // faub style failsafe
        if (config.isFaub()) {
            auto cacheEntryIt = config.fcache.getFirstBackup();
            while (cacheEntryIt != config.fcache.getEnd()) {
                descrip = "";
                if (cacheEntryIt->second.mtimeDayAge <= fd) {
                    ++minValidBackups;
                    descrip = " [valid for fs]";
                }
                
                DEBUG(D_prune)
                DFMT("failsafe: " << cacheEntryIt->second.getDir()
                     << " (age=" << cacheEntryIt->second.mtimeDayAge << ")" << descrip);
                ++cacheEntryIt;
                
                if (minValidBackups >= fb)
                    break;
            }
        }
        // single-file style failsafe
        else {
            for (auto &fnameIdx : config.cache.indexByFilename) {
                auto raw_it = config.cache.rawData.find(fnameIdx.second);
                
                descrip = "";
                if (raw_it != config.cache.rawData.end() && raw_it->second.fnameDayAge <= fd) {
                    ++minValidBackups;
                    descrip = " [valid for fs]";
                }
                
                DEBUG(D_prune) DFMT("failsafe: " << raw_it->second.filename << " (age=" << raw_it->second.fnameDayAge
                                    << ")" << descrip);
                
                if (minValidBackups >= fb)
                    break;
            }
        }
        
        if (minValidBackups < fb) {
            string message = "skipping pruning due to failsafe check; only " +
            plural(minValidBackups, "backup") +
            " within the last " + plural(fd, "day") + "; " +
            to_string(fb) + " required";
            
            SCREENERR("warning: " << message);
            log(config.ifTitle() + " " + message);
            return false;
        }
        
        DEBUG(D_prune) DFMT("failsafe passed with " << plural(minValidBackups, "backup") << " ("
                            << fb << " required) in the last " << plural(fd, "day"));
    }
    
    return true;
}


/*******************************************************************************
 * pruneBackups(config)
 *
 * Apply the full rentetion policy inclusive of all safety checks.
 *******************************************************************************/
void pruneBackups(BackupConfig &config) {
    if (config.isFaub()) {
        pruneFaubBackups(config);
        
        if (!GLOBALS.cli.count(CLI_TEST)) {
            DEBUG(D_prune) DFMT("removing empty directories");
            config.removeEmptyDirs();
        }
        
        return;
    }
    
    set<string> changedMD5s;
    DEBUG(D_prune) DFMT("weeklies set to dow " << dw(config.settings[sDOW].ivalue()));
    
    size_t backupAge = 0, backupCountOnDay = 0, backupsPruned = 0;
    auto fsSlowLimit = config.settings[sFailsafeSlow].ivalue();
    
    // loop through the filename index sorted by filename (i.e. all backups by age)
    for (auto fIdx_it = config.cache.indexByFilename.begin(), next_it = fIdx_it;
         fIdx_it != config.cache.indexByFilename.end(); fIdx_it = next_it) {
        // the second iterator (next_it) is necessary because a function called within
        // this loop (config.cache.remove()) calls erase() on our primary iterator. while
        // that appears to be handled on darwin it crashes under linux. next_it allows
        // the loop to track the next value for the iterator without dereferencing a deleted
        // pointer.
        ++next_it;
        
        if (fsSlowLimit && backupsPruned >= fsSlowLimit) {
            DEBUG(D_prune) DFMT("failsafe_slow limit reached (" << plural(fsSlowLimit, "backup") << " pruned, aborting further prunes");
            break;
        }
        
        auto raw_it = config.cache.rawData.find(fIdx_it->second);
        
        if (raw_it != config.cache.rawData.end()) {
            unsigned long filenameAge = raw_it->second.fnameDayAge;
            int filenameDOW = raw_it->second.dow;
            
            auto shouldKeep = pruneShouldKeep(config, raw_it->second.filename, (int)filenameAge,
                                              filenameDOW, raw_it->second.date_day,
                                              raw_it->second.date_month, raw_it->second.date_year);
            
            if (backupAge != filenameAge) {
                backupAge = filenameAge;
                backupCountOnDay = 1;
            }
            else
                backupCountOnDay += 1;
            
            // if standard retention pruning deletes this backup and there's only one other
            // backup on this day, we don't want consolidation to delete the other one
            if (!shouldKeep.length()) backupCountOnDay -= 1;
            
            auto shouldConsolidate = config.settings[sConsolidate].ivalue() && backupAge >= config.settings[sConsolidate].ivalue() && backupCountOnDay > 1;
            
            
            if (shouldKeep.length() && !shouldConsolidate) {
                DEBUG(D_prune) DFMT(shouldKeep);
                continue;
            }
            
            if (GLOBALS.cli.count(CLI_TEST))
                cout << YELLOW << config.ifTitle() << " TESTMODE: would have deleted "
                << raw_it->second.filename << " (age=" + to_string(filenameAge)
                << ", dow=" + dw(filenameDOW) << ")" << (shouldConsolidate ? " consolidation" : "") << RESET << endl;
            else {
                // delete the file and remove it from all caches
                if (!unlink(raw_it->second.filename.c_str())) {
                    NOTQUIET && cout << "\t• removed " << raw_it->second.filename << (shouldConsolidate ? " (consolidation)" : "") << endl;
                    log(config.ifTitle() + " removed " + raw_it->second.filename +
                        " (age=" + to_string(filenameAge) + ", dow=" + dw(filenameDOW) + ")" + (shouldConsolidate ? " consolidation" : ""));
                    
                    auto fname = raw_it->second.filename;
                    changedMD5s.insert(raw_it->second.md5);
                    config.cache.remove(raw_it->second);
                    config.cache.updated = true;  // updated causes the cache to get saved in the BackupCache destructor
                    DEBUG(D_prune) DFMT("completed removal of " << fname);
                    ++backupsPruned;
                }
                else {
                    log(config.ifTitle() + " unable to remove " + (shouldConsolidate ? " (consolidation) " : "") + raw_it->second.filename + errtext());
                    SCREENERR(string("unable to remove ") + (shouldConsolidate ? " (consolidation) " : "") + raw_it->second.filename + errtext());
                }
            }
        }
    }
    
    if (!GLOBALS.cli.count(CLI_TEST)) {
        DEBUG(D_prune) DFMT("removing empty directories");
        config.removeEmptyDirs();
        
        for (auto &md5 : changedMD5s) {
            DEBUG(D_prune) DFMT("re-stating " << md5);
            config.cache.reStatMD5(md5);
            DEBUG(D_prune) DFMT("re-stat complete");
        }
        
        if (changedMD5s.size())
            config.cache.saveAtEnd();
    }
}

/*******************************************************************************
 * updateLinks(config)
 *
 * Hard link identical backups together to save space.  This is for single-file
 * backups only.
 *******************************************************************************/
void updateLinks(BackupConfig &config)
{
    unsigned int maxLinksAllowed = config.settings[sMaxLinks].ivalue();
    bool includeTime = str2bool(config.settings[sIncTime].value);
    bool rescanRequired;
    unsigned long maxedOutLinkedFiles = 0;
    
    /* The indexByMD5 is a list of lists ("map" of "set"s).  Here we loop through the list of MD5s
     * (the map) once.  For each MD5, we loop through its list of associated files (the set) twice:
     *     - 1st time to find the file with the greatest number of existing hard links (call this
     * the reference file)
     *     - 2nd time to relink any individual file to the reference file
     * There are a number of caveats.  We don't relink any file that's less than a day old because
     * it may be being updated.  And we also account for the configured max number of hard links. If
     * that number is reached a subsequent grouping of linked files is started.
     */
    
    if (maxLinksAllowed < 2) return;
    
    // loop through list of MD5s (the map)
    set<string> changedMD5s;
    for (auto &md5 : config.cache.indexByMD5) {
        // only consider md5s with more than one file associated
        if (md5.second.size() < 2) continue;
        
        do {
            /* the rescan loop is a special case where we've hit maxLinksAllowed.  there may be more
             * files with that same md5 still to be linked, which would require a new inode.  for
             * that we need a new reference file.  setting rescanrequired to true takes us back here
             * to pick a new one.  */
            
            rescanRequired = false;
            
            // find the file matching this md5 with the greatest number of links.
            // some files matching this md5 may already be linked together while others
            // are not.  we need the one with the greatest number of links so that
            // we don't constantly relink the same files due to random selection.
            BackupEntry *referenceFile = NULL;
            unsigned int maxLinksFound = 0;
            
            DEBUG(D_link) DFMT("top of scan for " << md5.first);
            
            // 1st time: loop through the list of files (the set)
            for (auto &fileID : md5.second) {
                auto raw_it = config.cache.rawData.find(fileID);
                
                if (raw_it != config.cache.rawData.end()) {
                    DEBUG(D_link)
                    DFMT("ref loop - considering for ref file "
                         << raw_it->second.filename << " (links=" << raw_it->second.links
                         << ", found=" << maxLinksFound << ", max=" << maxLinksAllowed
                         << ", age=" << raw_it->second.fnameDayAge << ")");
                    
                    if (raw_it->second.links > maxLinksFound &&   // more links than previous files for this md5
                        raw_it->second.links < maxLinksAllowed && // still less than the configured max
                        raw_it->second.fnameDayAge) {                 // at least a day old (i.e. don't relink today's file)
                        
                        referenceFile = &raw_it->second;
                        maxLinksFound = raw_it->second.links;
                        
                        DEBUG(D_link)
                        DFMT("ref loop - new ref file selected " << referenceFile->md5 << " "
                             << referenceFile->filename
                             << "; links=" << maxLinksFound);
                    }
                }
            }
            
            // the first actionable file (one that's not today's file and doesn't already have links
            // at the maxLinksAllowed level) becomes the reference file.  if there's no reference
            // file there's nothing to do for this md5.  skip to the next one.
            if (referenceFile == NULL) {
                DEBUG(D_link) DFMT("no reference file found for " << md5.first);
                continue;
            }
            
            // 2nd time: loop through the list of files (the set)
            for (auto &fileID : md5.second) {
                auto raw_it = config.cache.rawData.find(fileID);
                DEBUG(D_link) DFMT("\tlink loop - examining " << raw_it->second.filename);
                
                if (raw_it != config.cache.rawData.end()) {
                    // skip the reference file; can't relink it to itself
                    if (referenceFile == &raw_it->second) {
                        DEBUG(D_link) DFMT("\t\treference file itself");
                        continue;
                    }
                    
                    // skip files that are already linked
                    if (referenceFile->inode == raw_it->second.inode) {
                        DEBUG(D_link) DFMT("\t\talready linked");
                        continue;
                    }
                    
                    // skip today's file as it could still be being updated
                    if (!raw_it->second.fnameDayAge && !includeTime) {
                        DEBUG(D_link) DFMT("\t\ttoday's file");
                        continue;
                    }
                    
                    // skip if this file already has the max links
                    if (raw_it->second.links >= maxLinksAllowed) {
                        DEBUG(D_link) DFMT("\t\tfile links already maxed out");
                        ++maxedOutLinkedFiles;
                        continue;
                    }
                    
                    // relink the file to the reference file
                    string detail = raw_it->second.filename + " <-> " + referenceFile->filename;
                    if (GLOBALS.cli.count(CLI_TEST))
                        cout << YELLOW << config.ifTitle() << " TESTMODE: would have linked "
                        << detail << RESET << endl;
                    else {
                        if (!unlink(raw_it->second.filename.c_str())) {
                            if (!link(referenceFile->filename.c_str(),
                                      raw_it->second.filename.c_str())) {
                                NOTQUIET &&cout << "\t• linked " << detail << endl;
                                log(config.ifTitle() + " linked " + detail);
                                
                                changedMD5s.insert(md5.first);
                                
                                if (referenceFile->links >= maxLinksAllowed) {
                                    rescanRequired = true;
                                    break;
                                }
                            }
                            else {
                                SCREENERR("error: unable to link " << detail << errtext());
                                log(config.ifTitle() + " error: unable to link " + detail + errtext());
                            }
                        }
                        else {
                            SCREENERR("error: unable to remove " << raw_it->second.filename
                                      << " in prep to link it"
                                      << errtext());
                            log(config.ifTitle() + " error: unable to remove " +
                                raw_it->second.filename + " in prep to link it" +
                                errtext());
                        }
                    }
                }
            }
        } while (rescanRequired);
    }
    
    for (auto &md5 : changedMD5s) {
        DEBUG(D_link) DFMT("re-stating " << md5);
        config.cache.reStatMD5(md5);
        DEBUG(D_link) DFMT("re-stat complete");
    }
    
    if (changedMD5s.size()) config.cache.saveAtEnd();
}

/*******************************************************************************
 * interpolate(command, subDir, fullDirectory, basicFilename)
 *
 * Substitute variable values into SCP/SFP commands.
 *******************************************************************************/
string interpolate(string command, string subDir, string fullDirectory, string basicFilename)
{
    string origCmd = command;
    size_t pos;
    
    while ((pos = command.find(INTERP_SUBDIR)) != string::npos)
        command.replace(pos, string(INTERP_SUBDIR).length(), subDir);
    
    while ((pos = command.find(INTERP_FULLDIR)) != string::npos)
        command.replace(pos, string(INTERP_FULLDIR).length(), fullDirectory);
    
    while ((pos = command.find(INTERP_FILE)) != string::npos)
        command.replace(pos, string(INTERP_FILE).length(), basicFilename);
    
    DEBUG(D_any)
    DFMT("interpolate: [" << origCmd << "], " << subDir << ", " << fullDirectory << ", "
         << basicFilename << " = " << command);
    return (command);
}

/*******************************************************************************
 * sCpBackup(config, backupFilename, subDir, sCpParams)
 *
 * SCP the backup to the specified location.
 *******************************************************************************/
methodStatus sCpBackup(BackupConfig &config, string backupFilename, string subDir, string sCpParams)
{
    string sCpBinary = locateBinary("scp");
    timer sCpTime;
    
    // superfluous check as test mode bombs out of performBackup() long before it ever calls
    // sCpBackup(). but just in case future logic changes, testing here
    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW << config.ifTitle() + " TESTMODE: would have SCPd " + backupFilename + " to "
        << sCpParams << RESET << endl;
        return methodStatus(true, "");
    }
    
    if (!sCpBinary.length()) {
        SCREENERR("\t• " << config.ifTitle()
                  << " SCP skipped (unable to locate 'scp' binary in the PATH)");
        return methodStatus(false, "\t• SCP: unable to locate 'scp' binary in the PATH");
    }
    
    string screenMessage = config.ifTitle() + " SCPing to " + sCpParams + "... ";
    string backspaces = string(screenMessage.length(), '\b');
    string blankspaces = string(screenMessage.length(), ' ');
    NOTQUIET &&ANIMATE &&cout << screenMessage << flush;
    
    // execute the scp
    sCpTime.start();
    int result = system(string(sCpBinary + " " + backupFilename + " " + sCpParams).c_str());
    
    sCpTime.stop();
    NOTQUIET &&ANIMATE &&cout << backspaces << blankspaces << backspaces << flush;
    
    if (result == -1 || result == 127)
        return methodStatus(
                            false, errorcom(config.ifTitle(),
                                            "SCP failed for " + backupFilename + ", error executing " + sCpBinary));
    else if (result != 0)
        return methodStatus(false, errorcom(config.ifTitle(), "SCP failed for " + backupFilename +
                                            " via " + sCpParams));
    else {
        string message = "SCP completed for " + backupFilename + " in " + sCpTime.elapsed();
        log(config.ifTitle() + " " + message);
        NOTQUIET &&cout << "\t• " << config.ifTitle() << " " << message << endl;
        return methodStatus(true, "\t• " + message);
    }
}

/*******************************************************************************
 * sFtpBackup(config, backupFilename, subDir, sFtpParams)
 *
 * SFTP the backup to the specified destination.
 *******************************************************************************/
methodStatus sFtpBackup(BackupConfig &config, string backupFilename, string subDir,
                        string sFtpParams) {
    string sFtpBinary = locateBinary("sftp");
    bool makeDirs = sFtpParams.find("//") == string::npos;
    strReplaceAll(sFtpParams, "//", "/");
    PipeExec sFtp(sFtpBinary + " " + sFtpParams);
    string command;
    timer sFtpTime;
    
    // superfluous check as test mode bombs out of performBackup() long before it ever calls
    // sFtpBackup(). but just in case future logic changes, testing here
    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW
        << config.ifTitle() + " TESTMODE: would have SFTPd via '" + sFtpParams +
        "' and uploaded " + subDir + "/" + backupFilename
        << RESET << endl;
        return methodStatus(true, "");
    }
    
    if (!sFtpBinary.length())
        return methodStatus(false,
                            errorcom(config.ifTitle(),
                                     "SFTP skipped (unable to locate 'sftp' binary in the PATH)"));
    
    string screenMessage = config.ifTitle() + " SFTPing via " + sFtpParams + "... ";
    string backspaces = string(screenMessage.length(), '\b');
    string blankspaces = string(screenMessage.length(), ' ');
    NOTQUIET &&ANIMATE &&cout << screenMessage << flush;
    DEBUG(D_transfer)
    {
        cout << "\n";
        DFMT("filename: " << backupFilename << "\nsubdir: " << subDir
             << "\nparams: " << sFtpParams);
    }
    
    // execute the sftp command
    sFtpTime.start();
    sFtp.execute(GLOBALS.cli.count(CLI_LEAVEOUTPUT) ? config.settings[sTitle].value : "");
    
    if (makeDirs) {
        char data[1500];
        strcpy(data, subDir.c_str());
        char *p = strtok(data, "/");
        string path;
        
        // make each component of the subdirectory "mkdir -p" style
        while (p) {
            path += p + string("/");
            command = "mkdir " + path + "\n";
            DEBUG(D_transfer) DFMT("SFTP: " << command);
            sFtp.ipcWrite(command.c_str(), command.length());
            p = strtok(NULL, "/");
        }
        
        // cd to the new subdirectory
        command = "cd " + subDir + "\n";
        DEBUG(D_transfer) DFMT("SFTP: " << command);
        sFtp.ipcWrite(command.c_str(), command.length());
    }
    
    // check disk space
    auto requiredSpace = approx2bytes(config.settings[sMinSFTPSpace].value);
    if (requiredSpace) {
        command = "df .\n";
        DEBUG(D_transfer) DFMT("SFTP: " << command);
        sFtp.ipcWrite(command.c_str(), command.length());
        auto freeSpace = sFtp.statefulReadAndMatchRegex(
                                                        "Avail.*%Capacity\n\\s*\\d+\\s+\\d+\\s+(\\d+)\\s+\\d+\\s+\\d+%");
        DEBUG(D_transfer) DFMT("SFTP: disk free returned [" << freeSpace << "]");
        
        if (freeSpace.length()) {
            auto availSpace = approx2bytes(freeSpace + "K");
            
            if (availSpace < requiredSpace) {
                NOTQUIET &&ANIMATE &&cout << backspaces << blankspaces << backspaces << flush;
                return methodStatus(
                                    false, errorcom(config.ifTitle(),
                                                    " SFTP aborted due to insufficient disk space (" +
                                                    approximate(availSpace) + ") on the remote server, " +
                                                    approximate(requiredSpace) + " required"));
            }
            else
                DEBUG(D_transfer)
                DFMT("\nSFTP space check passed (avail " << approximate(availSpace) << ", required "
                     << approximate(requiredSpace) << ")");
        }
        else {
            NOTQUIET &&ANIMATE &&cout << backspaces << blankspaces << backspaces << flush;
            return methodStatus(
                                false,
                                errorcom(config.ifTitle(),
                                         " SFTP aborted - unable to check free space (df) on the remote server: " +
                                         sFtp.errorOutput()));
        }
    }
    
    // upload the backup file
    command = "put " + backupFilename + "\n";
    DEBUG(D_transfer) DFMT("SFTP: " << command);
    sFtp.ipcWrite(command.c_str(), command.length());
    
    command = string("quit\n");
    DEBUG(D_transfer) DFMT("SFTP: " << command);
    sFtp.ipcWrite(command.c_str(), command.length());
    
    bool success = sFtp.readAndMatch("Uploading");
    DEBUG(D_transfer) DFMT("SFTP readAndMatch returned " << success);
    sFtp.closeAll();
    sFtpTime.stop();
    NOTQUIET &&ANIMATE &&cout << backspaces << blankspaces << backspaces << flush;
    
    if (success) {
        string message = "SFTP completed for " + backupFilename + " in " + sFtpTime.elapsed();
        NOTQUIET &&cout << "\t• " << config.ifTitle() + " " + message << endl;
        log(config.ifTitle() + " " + message);
        return methodStatus(true, "\t• " + message);
    }
    else
        return methodStatus(
                            false, errorcom(config.ifTitle(), "SFTP failed for " + backupFilename + " via " +
                                            sFtpParams + sFtp.errorOutput()));
}

/*******************************************************************************
 * performTripwireCheck(config)
 *
 * Verify the tripwire file, if defined
 *******************************************************************************/
bool performTripwire(BackupConfig &config)
{
    struct stat statData;
    
    if (config.settings[sTripwire].value.length()) {
        auto tripPairs = perlSplit("\\s*,\\s*", config.settings[sTripwire].value);
        
        for (string tripPair : tripPairs) {
            auto tripItem = perlSplit("\\s*:\\s*", tripPair);
            
            if (!mystat(tripItem[0], &statData)) {
                string md5 = MD5file(tripItem[0].c_str(), true);
                
                if (!md5.length()) {
                    DEBUG(D_tripwire) DFMT("FAIL - unable to MD5 tripwire file " << tripItem[0]);
                    log(config.ifTitle() + " unable to MD5 tripwire file " + tripItem[0]);
                    notify(config, "\t• Unable to MD5 tripwire file (" + tripItem[0] + ")\n",
                           false);
                    return false;
                }
                else if (md5 != tripItem[1]) {
                    DEBUG(D_tripwire) DFMT("FAIL - MD5 mismatch for tripwire file " << tripItem[0]);
                    log(config.ifTitle() + " tripwire file MD5 mismatch for " + tripItem[0]);
                    notify(config, "\t• Tripwire file MD5 mismatch (" + tripItem[0] + ")\n", false);
                    return false;
                }
                else {
                    DEBUG(D_tripwire) DFMT("MD5 verified for tripwire file " << tripItem[0]);
                    log(config.ifTitle() + " MD5 verified for tripwire file " + tripItem[0]);
                }
            }
            else {
                DEBUG(D_tripwire) DFMT("FAIL - unable to locate tripwire file " << tripItem[0]);
                log(config.ifTitle() + " unable to locate tripwire file " + tripItem[0]);
                notify(config, "\t• Unable to locate tripwire file to MD5 (" + tripItem[0] + ")\n",
                       false);
                return false;
            }
        }
    }
    
    return true;
}


/*******************************************************************************
 * performBackup(config)
 *
 * Perform a backup, saving it to the configured filename.
 *******************************************************************************/
void performBackup(BackupConfig &config) {
    bool incTime = str2bool(config.settings[sIncTime].value);
    string setFname = config.settings[sBackupFilename].value;
    string setDir = config.settings[sDirectory].value;
    string setCommand = config.settings[sBackupCommand].value;
    string tempExtension = ".tmp." + to_string(GLOBALS.pid);
    
    if (!setFname.length() || GLOBALS.cli.count(CLI_NOBACKUP) || !setCommand.length() || config.isFaub())
        return;
    
    // setup path names and filenames
    time_t now;
    char buffer[100];
    now = time(NULL);
    
    strftime(buffer, sizeof(buffer), incTime ? "%Y/%m/%d" : "%Y/%m", localtime(&now));
    string subDir = buffer;
    
    strftime(buffer, sizeof(buffer), incTime ? "-%Y-%m-%d-%T" : "-%Y-%m-%d", localtime(&now));
    string fnameInsert = buffer;
    
    string fullDirectory = slashConcat(setDir, subDir) + "/";
    string basicFilename;
    
    Pcre fnamePartsRE("(.*)(\\.[^.]+)$");
    if (fnamePartsRE.search(setFname) && fnamePartsRE.matches() > 1)
        basicFilename = fnamePartsRE.get_match(0) + fnameInsert + fnamePartsRE.get_match(1);
    else
        basicFilename = setFname + fnameInsert;
    string backupFilename = fullDirectory + basicFilename;
    
    if (GLOBALS.cli.count(CLI_TEST)) {
        cout << YELLOW << config.ifTitle() << " TESTMODE: would have begun backup to "
        << backupFilename << RESET << endl;
        return;
    }
    
    // make sure the destination directory exists
    mkdirp(fullDirectory);
    
    log(config.ifTitle() + " starting backup to " + backupFilename);
    string screenMessage =
    config.ifTitle() + " backing up to temp file " + backupFilename + tempExtension + "... ";
    string backspaces = string(screenMessage.length(), '\b');
    string blankspaces = string(screenMessage.length(), ' ');
    NOTQUIET &&ANIMATE &&cout << screenMessage << flush;
    
    // note start time
    timer backupTime;
    backupTime.start();
    
    // begin backing up
    PipeExec backup(setCommand);
    GLOBALS.interruptFilename = backupFilename + tempExtension;
    backup.execute2file(GLOBALS.interruptFilename,
                        GLOBALS.cli.count(CLI_LEAVEOUTPUT) ? config.settings[sTitle].value : "");
    GLOBALS.interruptFilename = "";  // interruptFilename gets cleaned up on SIGINT & SIGTERM
    
    // note finish time
    backupTime.stop();
    NOTQUIET &&ANIMATE &&cout << backspaces << blankspaces << backspaces << flush;
    
    // determine results
    struct stat statData;
    if (!mystat(string(backupFilename + tempExtension), &statData)) {
        if (statData.st_size >= approx2bytes(config.settings[sMinSize].value)) {
            backup.flushErrors();
            
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
            cacheEntry.updateAges(backupTime.getEndTimeSecs());
            cacheEntry.calculateMD5();
            
            // rename the file
            if (!rename(string(backupFilename + tempExtension).c_str(), backupFilename.c_str())) {
                auto uid = config.settings[sUID].ivalue();
                auto gid = config.settings[sGID].ivalue();
                
                // update the owner: -1 (default) means no change, 0 means set to the calling user,
                // x means set to user x.  setting to root (uid 0) can be achieved by running as
                // root (or suid) and setting -1 to make no change.
                if (uid > -1 || gid > -1)
                    chown(backupFilename.c_str(), uid == 0 ? getuid() : uid,
                          gid == 0 ? getgid() : gid);
                
                auto size = approximate(cacheEntry.size);
                
                string message = "backup completed to " + backupFilename + " (" + size + ") in " +
                backupTime.elapsed();
                log(config.ifTitle() + " " + message);
                NOTQUIET &&cout << "\t• " << config.ifTitle() << " " << message << endl;
                
                try {  // could get an exception converting settings[sMode] to an octal number
                    int mode = (int)strtol(config.settings[sMode].value.c_str(), NULL, 8);
                    
                    if (chmod(backupFilename.c_str(), mode)) {
                        SCREENERR("error: unable to chmod " << config.settings[sMode].value
                                  << " on " << backupFilename);
                        log("error: unable to chmod " + config.settings[sMode].value + " on " +
                            backupFilename);
                    }
                }
                catch (...) {
                    string err = string("error: invalid file mode specified (") +
                    config.settings[sMode].value.c_str() + ")";
                    SCREENERR(err);
                    log(err);
                }
                
                cacheEntry.filename = backupFilename;  // rename the file in the cache entry
                config.cache.addOrUpdate(cacheEntry, true, true);
                config.cache.reStatMD5(cacheEntry.md5);
                
                // let's commit the cache to disk now instead of via the destructor so that anyone
                // running a -0 or -1 while we're still finishing our SCP/SFTP in the background
                // won't have to recalculate the MD5 of the backup we just took.
                config.cache.saveCache();
                config.cache.updated = false;  // disable the now redundant save in the destructor
                
                bool overallSuccess = true;
                string notifyMessage = "\t• " + message + "\n";
                
                if (config.settings[sSFTPTo].value.length()) {
                    string sFtpParams = interpolate(config.settings[sSFTPTo].value, subDir,
                                                    fullDirectory, basicFilename);
                    auto sFTPStatus = sFtpBackup(config, backupFilename, subDir, sFtpParams);
                    overallSuccess &= sFTPStatus.success;
                    notifyMessage += "\n" + sFTPStatus.detail + "\n";
                }
                
                if (config.settings[sSCPTo].value.length()) {
                    string sCpParams = interpolate(config.settings[sSCPTo].value, subDir,
                                                   fullDirectory, basicFilename);
                    auto sCPStatus = sCpBackup(config, backupFilename, subDir, sCpParams);
                    overallSuccess &= sCPStatus.success;
                    notifyMessage += "\n" + sCPStatus.detail + "\n";
                }
                
                if (config.settings[sBloat].value.length()) {
                    string bloat = config.settings[sBloat].value;
                    auto target = config.getBloatTarget();
                    if (cacheEntry.size > target) {
                        notify(config, errorcom(config.ifTitle(), "warning: backup is larger than the bloat warning threshold (backup: " + to_string(cacheEntry.size) + ", threshold: " + bloat + ")"), false);
                        
                        backup.flushErrors();
                        return;
                    }
                }
                
                notify(config, notifyMessage, overallSuccess);
                backup.flushErrors();
            }
            else {
                unlink(string(backupFilename + tempExtension).c_str());
                notify(config,
                       errorcom(config.ifTitle(),
                                "backup failed, unable to rename temp file to " + backupFilename) +
                       "\n",
                       false);
                backup.flushErrors();
            }
        }
        else {
            unlink(string(backupFilename + tempExtension).c_str());
            notify(config,
                   errorcom(config.ifTitle(), "backup failed to " + backupFilename +
                            " (insufficient output/size: " +
                            to_string(statData.st_size) + " bytes)") +
                   "\n",
                   false);
        }
    }
    else
        notify(config,
               errorcom(config.ifTitle(), "backup command failed to generate any output") + "\n",
               false);
    
    /*
     periodically we need to walk the cache and throw out backups that no longer exist
     (likely manually deleted outside of managebackups).  after a backup is as good a
     time as any.
     */
    
    for (auto fIdx_it = config.cache.indexByFilename.begin(), next_it = fIdx_it; fIdx_it != config.cache.indexByFilename.end(); fIdx_it = next_it) {
        // the second iterator (next_it) is necessary because a function called within
        // this loop (config.cache.remove()) calls erase() on our primary iterator. next_it allows
        // the loop to track the next value for the iterator without dereferencing a deleted
        // pointer.
        ++next_it;
        
        if (!exists(fIdx_it->first)) {
            auto rawIt = config.cache.rawData.find(fIdx_it->second);
            if (rawIt != config.cache.rawData.end()) {
                log(config.ifTitle() + " " + fIdx_it->first + " has vanished, updating cache");
                config.cache.remove(rawIt->second);
                config.cache.updated = true;
            }
        }
    }
}

/*******************************************************************************
 * setupUserDirectories()
 *
 * Configure the 3 primary app directories (config, cache, log) to be relative
 * to the current user's home directory.
 *******************************************************************************/
void setupUserDirectories()
{
    struct passwd *pws;
    if ((pws = getpwuid(getuid())) == NULL) {
        SCREENERR("error: unable to lookup current user");
        log("error: unable to lookup current user via getpwuid()");
        exit(1);
    }
    
    string mbs = "managebackups";
    GLOBALS.confDir = slashConcat(pws->pw_dir, mbs, "/etc");
    GLOBALS.cacheDir = slashConcat(pws->pw_dir, mbs, "/var/cache");
    GLOBALS.logDir = slashConcat(pws->pw_dir, mbs, "/var/log");
}

/*******************************************************************************
 * sigTermHandler(sig)
 *
 * Catch the configured signals and clean up any inprocess backup.
 *******************************************************************************/
void sigTermHandler(int sig) {
    string reason = (sig > 0 ? "interrupt" : !sig ? "timeout" : "error");
    
    if (GLOBALS.interruptFilename.length()) {
        log("operation aborted on " + reason + (sig ? ", signal " + to_string(sig) : "") + " (" +
            GLOBALS.interruptFilename + ")");

        cerr << "\n" << reason << ": aborting backup, cleaning up " << GLOBALS.interruptFilename << "... ";
        
        struct stat statData;
        if (!mystat(GLOBALS.interruptFilename, &statData)) {
            if (S_ISDIR(statData.st_mode)) {
                rename(GLOBALS.interruptFilename.c_str(), string(GLOBALS.interruptFilename + ".abandoned").c_str());
                rmrf(GLOBALS.interruptFilename + ".abandoned");             // faub-style
            }
            else
                unlink(GLOBALS.interruptFilename.c_str());   // single-file
        }
        
        cerr << "done." << endl;
    }
    else
        log("operation aborted on " + reason + (sig > 0 ? " (signal " + to_string(sig) + ")" : ""));
    
    if (GLOBALS.interruptLock.length()) unlink(GLOBALS.interruptLock.c_str());
    
    exit(1);
}


// descriptive function name for exit
void cleanupAndExitOnError() {
    sigTermHandler(-1);
}


bool enoughLocalSpace(BackupConfig &config) {
    auto requiredSpace = approx2bytes(config.settings[sMinSpace].value);
    
    DEBUG(D_backup) DFMT("required for backup: " << requiredSpace);
    if (!requiredSpace)
        return true;
    
    struct statfs fs;
    if (!statfs(config.settings[sDirectory].value.c_str(), &fs)) {
        auto availableSpace = (uint64_t)fs.f_bsize * fs.f_bavail;
        
        DEBUG(D_backup)
        DFMT(config.settings[sDirectory].value
             << ": available=" << availableSpace << " (" << approximate(availableSpace)
             << "), required=" << requiredSpace << " (" << approximate(requiredSpace) << ")");
        
        if (availableSpace < requiredSpace) {
            notify(config, errorcom(config.ifTitle(), "error: insufficient space (" +
                                    approximate(availableSpace) + ") to start a new backup, " +
                                    approximate(requiredSpace) + " required."), true);
            return false;
        }
    }
    else {
        log("error: unable to statvfs() the filesystem that " + config.settings[sDirectory].value +
            " is on (errno " + to_string(errno) + ")");
        SCREENERR("error: unable to statvfs() the filesystem that "
                  << config.settings[sDirectory].value << " is on (errno " << errno << ")");
    }
    
    return true;
}


struct crossFSDataType {
    map<ino_t, string> iMap;
    string newDir;
};


bool crossFSCallback(pdCallbackData &file) {
    crossFSDataType *newDataPtr;
    newDataPtr = (crossFSDataType*)file.dataPtr;
    
    string oldFilename = file.filename;
    string newFilename = oldFilename;
    newFilename.replace(0, file.origDir.length(), "");
    newFilename = slashConcat(newDataPtr->newDir, newFilename);
    
    if (!mylstat(oldFilename, &file.statData)) {
        if (S_ISDIR(file.statData.st_mode))
            mkdirp(newFilename, file.statData);
        else {
            auto inodeEntry = newDataPtr->iMap.find(file.statData.st_ino);
            
            // duplicate hard links
            if (inodeEntry != newDataPtr->iMap.end()) {
                if (link(inodeEntry->second.c_str(), newFilename.c_str())) {
                    SCREENERR("error: unable to create hard link " + newFilename + errtext());
                    exit(1);
                }
            }
            else {
                // translate symlinks
                if (S_ISLNK(file.statData.st_mode)) {
                    char linkPath[PATH_MAX];
                    int len = (int)readlink(oldFilename.c_str(), linkPath, sizeof(linkPath));
                    linkPath[len] = 0;
                    if (len > -1) {
                        if (symlink(linkPath, newFilename.c_str())) {
                            
                            if (errno == EEXIST && GLOBALS.cli.count(CLI_FORCE)) {
                                if (!unlink(newFilename.c_str()) && !symlink(linkPath, newFilename.c_str()))
                                    setFilePerms(newFilename, file.statData);
                                else {
                                    SCREENERR("error: unable to recreate existing symlink " + newFilename + " to " + linkPath + errtext());
                                    exit(1);
                                }
                            }
                            else {
                                SCREENERR("error: unable to create symlink " + newFilename + " to " + linkPath + errtext());
                                exit(1);
                            }
                        }
                        else
                            setFilePerms(newFilename, file.statData);
                    }
                    else {
                        SCREENERR("error: unable to read symlink " + oldFilename + errtext());
                        exit(1);
                    }
                }
                else {
                    // copy regular files
                    if (!copyFile(oldFilename, newFilename)) {
                        SCREENERR("error: unable to copy " + oldFilename + " to " + newFilename + errtext());
                        exit(1);
                    }
                    
                    newDataPtr->iMap.insert(newDataPtr->iMap.end(), make_pair(file.statData.st_ino, newFilename));
                    setFilePerms(newFilename, file.statData);
                }
            }
        }
    }
    else {
        SCREENERR("error: cross-filesystem move interrupted, unable to stat " + oldFilename + errtext());
        exit(1);
    }
    
    return true;
}


void moveBackupCrossFS(string oldBackupDir, string newBackupDir, crossFSDataType& fsData) {
    struct stat statData;
    
    // create the top-level directory
    if (!mylstat(oldBackupDir, &statData))
        mkdirp(newBackupDir, statData);
    else {
        SCREENERR("error: unable to stat " + oldBackupDir + errtext());
        exit(1);
    }
    
    fsData.newDir = newBackupDir;
    processDirectory(oldBackupDir, "", false, false, crossFSCallback, &fsData);
}


bool moveBackup(string fullBackupOldPath, string oldBaseDir, string newBaseDir, bool sameFS, unsigned long numBackups, crossFSDataType& fsData) {
    static int count = 0;
    bool result = true;
    struct stat statData;
    auto fullBackupNewPath = fullBackupOldPath;
    auto pos = fullBackupNewPath.find(oldBaseDir);
    if (pos != string::npos)
        fullBackupNewPath.replace(pos, oldBaseDir.length(), "");
    fullBackupNewPath = slashConcat(newBaseDir, fullBackupNewPath);
    
    auto ps = pathSplit(fullBackupNewPath);
    if (mkdirp(ps.dir)) {
        SCREENERR("error: unable to mkdir " << ps.dir);
        exit(1);
    }
    
    if (sameFS) {
        if (exists(fullBackupOldPath)) {
            if (rename(fullBackupOldPath.c_str(), fullBackupNewPath.c_str())) {
                
                if (errno != ENOENT || !GLOBALS.cli.count(CLI_FORCE) || mystat(fullBackupNewPath, &statData)) {
                    SCREENERR("error: unable to rename " << fullBackupOldPath << " to " << fullBackupNewPath << errtext());
                    if (count)
                        SCREENERR("\nSome backups were successfully renamed. Correct the permission issue and rerun\n" <<
                                  "the --relocate to maintain a consistent state.  Use --force if necessary.");
                    exit(1);
                }
            }
        }
        else
            if (!GLOBALS.cli.count(CLI_FORCE) || !exists(fullBackupNewPath))
                result = false;
        /* else
         we silently consider it a success becauase the source is missing but
         the destination exists and --force was specified.
         */
    }
    else {
        moveBackupCrossFS(fullBackupOldPath, fullBackupNewPath, fsData);
        rmrf(fullBackupOldPath);
    }
    
    ++count;
    return result;
}


void relocateBackups(BackupConfig &config, string newBaseDir) {
    char dirBuf[PATH_MAX+1];
    
    if (newBaseDir.find("*") != string::npos || newBaseDir.find("?") != string::npos) {
        SCREENERR("error: specific directory required (no wildcards)");
        exit(1);
    }
    
    // prepend pwd
    if (newBaseDir[0] != '/' && newBaseDir[0] != '~') {
        if (getcwd(dirBuf, sizeof(dirBuf)) == NULL) {
            SCREENERR("error: unable to determine the current directory");
            exit(1);
        }
        
        newBaseDir = slashConcat(dirBuf, newBaseDir);
    }
    
    // handle tilde substitution
    // most of the time this isn't necessary because the shell does the
    // substitution before our app is invoked
    if (newBaseDir[0] == '~') {
        string user;
        auto slash = newBaseDir.find("/");
        
        if (slash != string::npos)
            user = newBaseDir.substr(1, slash - 1);
        else
            user = newBaseDir.length() ? newBaseDir.substr(1, newBaseDir.length() - 1) : "";
        
        auto uid = getUidFromName(user);
        if (uid < 0) {
            SCREENERR("error: invalid user reference in ~" << user);
            exit(1);
        }
        
        auto homeDir = getUserHomeDir(uid);
        if (!homeDir.length()) {
            SCREENERR("error: unable to determine home directory for ~" + user);
            exit(1);
        }
        
        newBaseDir.replace(0, slash, homeDir);
    }
    
    auto oldBaseDir = config.isFaub() ? config.fcache.getBaseDir() : config.settings[sDirectory].value;
    
    if (newBaseDir == oldBaseDir) {
        NOTQUIET && cout << GREEN << "backups for " << config.settings[sTitle].value << " are already in " << oldBaseDir << RESET << endl;
        exit(0);
    }
    
    if (mkdirp(newBaseDir)) {
        SCREENERR("error: unable to create directory " << newBaseDir << errtext());
        exit(1);
    }
    
    struct stat statData1;
    struct stat statData2;
    
    if (mystat(oldBaseDir, &statData1)) {
        SCREENERR("error: unable to access (stat) " << oldBaseDir << errtext());
        exit(1);
    }
    
    if (mystat(newBaseDir, &statData2)) {
        SCREENERR("error: unable to access (stat) " << newBaseDir << errtext());
        exit(1);
    }
    
    bool sameFS = (statData1.st_dev == statData2.st_dev);
    
    auto numBackups = config.isFaub() ? config.fcache.getNumberOfBackups() : config.cache.rawData.size();
    NOTQUIET && ANIMATE && cout << "moving backups " << (sameFS ? "[local filesystem]... " : "[cross-filesystem]... ");
    
    // declare the crossFSDataType to hold the inode map.  we pass this to all the moveBackup() calls so it can use it
    // to tell which files need to be hardlinked to each other, even across separate backups
    crossFSDataType fsData;
    progressPercentageA(-1);
    
    // actually move the backup files
    if (config.isFaub()) {
        auto backupIt = config.fcache.getFirstBackup();
        while (backupIt != config.fcache.getEnd()) {
            NOTQUIET && ANIMATE && cout << progressPercentageA((int)numBackups, 1, (int)distance(config.fcache.getFirstBackup(), backupIt), 1, backupIt->second.getDir()) << flush;
            if (!moveBackup(backupIt->second.getDir(), oldBaseDir, newBaseDir, sameFS, numBackups, fsData)) {
                log(config.ifTitle() + " " + backupIt->second.getDir() + " has vanished, updating cache");
                config.fcache.removeBackup(backupIt);
            }
            ++backupIt;
        }
    }
    else
        for (auto fIdx_it = config.cache.indexByFilename.begin(), next_it = fIdx_it;
             fIdx_it != config.cache.indexByFilename.end(); fIdx_it = next_it) {
            // the second iterator (next_it) is necessary because a function called within
            // this loop (config.cache.remove()) calls erase() on our primary iterator. next_it allows
            // the loop to track the next value for the iterator without dereferencing a deleted
            // pointer.
            ++next_it;
            
            NOTQUIET && ANIMATE && cout << progressPercentageA((int)numBackups, 1, (int)distance(config.cache.indexByFilename.begin(), fIdx_it), 1, fIdx_it->first) << flush;
            
            if (!moveBackup(fIdx_it->first, oldBaseDir, newBaseDir, sameFS, numBackups, fsData)) {
                auto rawIt = config.cache.rawData.find(fIdx_it->second);
                if (rawIt != config.cache.rawData.end()) {
                    log(config.ifTitle() + " " + fIdx_it->first + " has vanished, updating cache");
                    config.cache.remove(rawIt->second);
                    config.cache.updated = true;
                }
            }
        }
    
    NOTQUIET && ANIMATE && cout << progressPercentageA(0) << "\nupdating config..." << flush;
    
    // clean up the directory name (resolve symlinks, and ".." references)
    // since mv succeeded above realpath() should succeed
    newBaseDir = realpathcpp(newBaseDir);
    
    // update the directory in the config file
    config.renameBaseDirTo(newBaseDir);
    
    NOTQUIET && ANIMATE && cout << "\nupdating caches..." << flush;
    
    // update the directory in all the cache files
    if (config.isFaub())
        config.fcache.renameBaseDirTo(newBaseDir);
    else {
        config.cache.setUUID(config.settings[sUUID].value);
        config.cache.saveCache(oldBaseDir, newBaseDir);
        config.cache.restoreCache(true);
    }
    
    NOTQUIET && ANIMATE && cout << endl;
    NOTQUIET && cout << GREEN << log(config.ifTitle() + " " + plural(numBackups, "backup") + " successfully relocated from " + oldBaseDir + " to " + newBaseDir) << RESET << endl;
    exit(0);
}


void saveGlobalStats(unsigned long stats, unsigned long md5s, string elapsedTime) {
    ofstream ofile;
    
    ofile.open(GLOBALS.cacheDir + "/" + GLOBALSTATSFILE);
    if (ofile.is_open()) {
        ofile << stats << endl;
        ofile << md5s << endl;
        ofile << elapsedTime << endl;
        
        ofile.close();
    }
    
    if (getuid() != geteuid())
        chown(string(GLOBALS.cacheDir + "/" + GLOBALSTATSFILE).c_str(), geteuid(), getegid());
}


bool getGlobalStats(unsigned long& stats, unsigned long& md5s, string& elapsedTime) {
    fstream ifile;
    
    ifile.open(GLOBALS.cacheDir + "/" + GLOBALSTATSFILE, ios::in);
    if (ifile.is_open()) {
        try {
            string data;
            
            getline(ifile, data);
            stats = stol(data);
            
            getline(ifile, data);
            md5s = stol(data);
            
            getline(ifile, elapsedTime);
            
            ifile.close();
        }
        catch (...) {
            unlink(string(GLOBALS.cacheDir + "/" + GLOBALSTATSFILE).c_str());
            return false;
        }
        
        return true;
    }
    
    return false;
}

void showMatches(Pcre& p, string foo) {
    if (p.search(foo)) {
        cout << "fish count: " << p.matches() << endl;
        cout << "0: '" << p.get_match(0) << "'" << endl;;
        cout << "1: '" << p.get_match(1) << "'" << endl;
        cout << "2: '" << p.get_match(2) << "'" << endl;
        cout << "3: '" << p.get_match(3) << "'" << endl;
        cout << endl;
    }
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
    GLOBALS.sessionId = rand();
    
    // default directories
    GLOBALS.confDir = CONF_DIR;
    GLOBALS.cacheDir = CACHE_DIR;
    
    // overwrite with env vars (if any)
    string temp;
    temp = cppgetenv("MB_CONFDIR");
    if (temp.length()) GLOBALS.confDir = temp;
    
    temp = cppgetenv("MB_CACHEDIR");
    if (temp.length()) GLOBALS.cacheDir = temp;
    
    temp = cppgetenv("MB_LOGDIR");
    if (temp.length()) GLOBALS.logDir = temp;
    
    time(&GLOBALS.startupTime);
    openlog("managebackups", LOG_PID | LOG_NDELAY, LOG_LOCAL1);
    cxxopts::Options options("managebackups", "Create and manage backups");
    
    options.add_options()(string("p,") + CLI_PROFILE, "Profile", cxxopts::value<std::string>())(
        string("d,") + CLI_DAYS, "Days", cxxopts::value<int>())(
        string("w,") + CLI_WEEKS, "Weeks", cxxopts::value<int>())(
        string("m,") + CLI_MONTHS, "Months", cxxopts::value<int>())(
        string("y,") + CLI_YEARS, "Years", cxxopts::value<int>())(
        string("c,") + CLI_COMMAND, "Command", cxxopts::value<std::string>())(
        string("n,") + CLI_NOTIFY, "Notify", cxxopts::value<std::string>())(
        string("V,") + CLI_VERSION, "Version", cxxopts::value<bool>()->default_value("false"))(
        string("q,") + CLI_QUIET, "No output", cxxopts::value<bool>()->default_value("false"))(
        string("l,") + CLI_MAXLINKS, "Max hard links", cxxopts::value<int>())(
        string("u,") + CLI_USER, "User", cxxopts::value<bool>()->default_value("false"))(
        string("a,") + CLI_ALLSEQ, "All sequential", cxxopts::value<bool>()->default_value("false"))(
        string("A,") + CLI_ALLPAR, "All parallel", cxxopts::value<bool>()->default_value("false"))(
        string("z,") + CLI_ZERO, "No animation (internal)", cxxopts::value<bool>()->default_value("false"))(
        string("t,") + CLI_TEST, "Test only mode", cxxopts::value<bool>()->default_value("false"))(
        string("x,") + CLI_LOCK, "Lock profile", cxxopts::value<bool>()->default_value("false"))(
        string("k,") + CLI_CRONS, "Cron", cxxopts::value<bool>()->default_value("false"))(
        string("K,") + CLI_CRONP, "Cron", cxxopts::value<bool>()->default_value("false"))(
        string("h,") + CLI_HELP, "Show help", cxxopts::value<bool>()->default_value("false"))(
        string("b,") + CLI_USEBLOCKS, "Use blocks disk usage", cxxopts::value<bool>()->default_value("false"))(
        string("s,") + CLI_PATHS, "Faub paths", cxxopts::value<std::vector<std::string>>())(
        string("f,") + CLI_FORCE, "Force various things", cxxopts::value<bool>()->default_value("false"))(
        string("g,") + CLI_GO, "Begin processing default profile", cxxopts::value<bool>()->default_value("false"))(
        CLI_FILE, "Filename", cxxopts::value<std::string>())(
        CLI_LAST, "Last diff", cxxopts::value<bool>()->default_value("false"))(
        CLI_FAUB, "Faub backup", cxxopts::value<std::string>())(
        CLI_NOS, "Notify on success", cxxopts::value<bool>()->default_value("false"))(
        CLI_SAVE, "Save config", cxxopts::value<bool>()->default_value("false"))(
        CLI_FS_BACKUPS, "Failsafe Backups", cxxopts::value<int>())(
        CLI_FS_DAYS, "Failsafe Days", cxxopts::value<int>())(
        CLI_FS_SLOW, "Failsafe Slow", cxxopts::value<int>())(
        CLI_FS_FP, "Failsafe Paranoid", cxxopts::value<bool>()->default_value("false"))(
        CLI_DIR, "Directory", cxxopts::value<std::string>())(
        CLI_SCPTO, "SCP to", cxxopts::value<std::string>())(
        CLI_SFTPTO, "SFTP to", cxxopts::value<std::string>())(
        CLI_NICE, "Nice value", cxxopts::value<int>())(
        CLI_MAILFROM, "Send mail from", cxxopts::value<std::string>())(
        CLI_STATS1, "Stats summary", cxxopts::value<bool>()->default_value("false"))(
        CLI_STATS2, "Stats detail", cxxopts::value<bool>()->default_value("false"))(
        CLI_PRUNE, "Enable pruning", cxxopts::value<bool>()->default_value("false"))(
        CLI_NOPRUNE, "Disable pruning", cxxopts::value<bool>()->default_value("false"))(
        CLI_DEFAULTS, "Show defaults", cxxopts::value<bool>()->default_value("false"))(
        CLI_DEFAULT, "Set as default", cxxopts::value<bool>()->default_value("false"))(
        CLI_TIME, "Include time", cxxopts::value<bool>()->default_value("false"))(
        CLI_NOBACKUP, "Don't backup", cxxopts::value<bool>()->default_value("false"))(
        CLI_NOCOLOR, "Disable color", cxxopts::value<bool>()->default_value("false"))(
        CLI_CONFDIR, "Configuration directory", cxxopts::value<std::string>())(
        CLI_CACHEDIR, "Cache directory", cxxopts::value<std::string>())(
        CLI_LOGDIR, "Log directory", cxxopts::value<std::string>())(
        CLI_DOW, "Day of week for weeklies", cxxopts::value<int>())(
        CLI_MODE, "File mode", cxxopts::value<std::string>())(
        CLI_MINSPACE, "Minimum local space", cxxopts::value<std::string>())(
        CLI_MINSFTPSPACE, "Minimum SFTP space", cxxopts::value<std::string>())(
        CLI_RECREATE, "Recreate config", cxxopts::value<bool>()->default_value("false"))(
        CLI_INSTALLMAN, "Install man", cxxopts::value<bool>()->default_value("false"))(
        CLI_INSTALL, "Install", cxxopts::value<bool>()->default_value("false"))(
        CLI_INSTALLSUID, "Install SUID", cxxopts::value<bool>()->default_value("false"))(
        CLI_NOTIFYEVERY, "Notify every", cxxopts::value<int>())(
        CLI_UID, "Owner UID", cxxopts::value<int>())(
        CLI_GID, "Owner GID", cxxopts::value<int>())(
        CLI_LEAVEOUTPUT, "Leave output", cxxopts::value<bool>()->default_value("false"))(
        CLI_SCHED, "Schedule runs", cxxopts::value<int>())(
        CLI_SCHEDHOUR, "Schedule hour", cxxopts::value<int>())(
        CLI_SCHEDMIN, "Schedule minute", cxxopts::value<int>())(
        CLI_SCHEDPATH, "Schedule path", cxxopts::value<string>())(
        CLI_COMPARE, "Compare two backups", cxxopts::value<vector<string>>())(
        CLI_COMPAREFILTER, "Compare two backups - filter", cxxopts::value<vector<string>>())(
        CLI_THRESHOLD, "Comparison threshold", cxxopts::value<string>())(
        CLI_CONSOLIDATE, "Consolidate backups after days", cxxopts::value<int>())(
        CLI_RECALC, "Recalculate faub space", cxxopts::value<bool>()->default_value("false"))(
        CLI_BLOAT, "Bloat size warning", cxxopts::value<string>())(
        CLI_RELOCATE, "Relocate", cxxopts::value<std::string>())(
        CLI_DATAONLY, "Data only", cxxopts::value<bool>()->default_value("false"))(
        CLI_INCLUDE, "Include", cxxopts::value<std::string>())(
        CLI_EXCLUDE, "Exclude", cxxopts::value<std::string>())(
        CLI_FILTERDIRS, "Filter directories", cxxopts::value<bool>()->default_value("false"))(
        CLI_TRIPWIRE, "Tripwire", cxxopts::value<std::string>());
    
    try {
        options.allow_unrecognised_options();  // to support -v...
        GLOBALS.cli = options.parse(argc, argv);
        GLOBALS.color = !(GLOBALS.cli[CLI_QUIET].as<bool>() || GLOBALS.cli[CLI_NOCOLOR].as<bool>());
        GLOBALS.useBlocks = GLOBALS.cli.count(CLI_USEBLOCKS);
        
        if (GLOBALS.cli.count(CLI_USER))
            setupUserDirectories();
        
        if (GLOBALS.cli.count(CLI_CONFDIR))
            GLOBALS.confDir = GLOBALS.cli[CLI_CONFDIR].as<string>();
        
        if (GLOBALS.cli.count(CLI_CACHEDIR))
            GLOBALS.cacheDir = GLOBALS.cli[CLI_CACHEDIR].as<string>();
        
        if (GLOBALS.cli.count(CLI_LOGDIR))
            GLOBALS.logDir = GLOBALS.cli[CLI_LOGDIR].as<string>();
        
        /* Enable selective debugging
         * (code taken from Exim MTA - Philip Hazel)
         */
        GLOBALS.debugSelector = 0;
        for (auto uarg : GLOBALS.cli.unmatched()) {
            if (uarg == "--vv") {
                GLOBALS.debugSelector = D_all;
            }
            else if (uarg.length() > 2) {
                string op = uarg.substr(2, 1);
                
                if (uarg.substr(0, 2) == "-v" && (op == "=" | op == "-" | op == "+")) {
                    unsigned int selector = D_default;
                    string remainder = uarg.substr(2, string::npos);
                    uschar *usc = (uschar *)remainder.c_str();
                    decode_bits(&selector, 1, debug_notall, usc, debug_options, ndebug_options);
                    GLOBALS.debugSelector = selector;
                    continue;
                }
            }
            else if (uarg == "-v") {
                GLOBALS.debugSelector = D_default;
                continue;
            }
            
            /* ----------------------------------- */
            
            SCREENERR("error: unrecognized parameter " << uarg
                      << "\nUse --help for a list of options.");
            exit(1);
        }
        
        GLOBALS.stats = GLOBALS.cli.count(CLI_STATS1) || GLOBALS.cli.count(CLI_STATS2) || argc == 1 || (argc == 2 && GLOBALS.debugSelector);
    }
    catch (cxxopts::OptionParseException &e) {
        cerr << "managebackups: " << e.what() << endl;
        exit(1);
    }
    
    /*
    if (argc == 1) {
        showHelp(hSyntax);
        exit(0);
    }
    */
    
    /*string foo = "; transfered ";
    auto b1 = string(foo.length(), '\b');
    auto b2 = string(foo.length(), ' ');
    
    cout << progressPercentage((int)50, 100, 2, 3) << flush;
    cout << foo;
    sleep(1);
    cout << progressPercentage((long)100, (long)25) << flush;
    sleep(1);
    cout << progressPercentage((long)100, (long)40) << flush;
    sleep(1);
    cout << progressPercentage((long)100, (long)50) << flush;
    sleep(1);
    cout << progressPercentage((long)0, (long)0) << flush;
    sleep(1);
    cout << b1 << b2 << b1 << flush;
    sleep(1);
    cout << progressPercentage((int)0) << flush;
    sleep(1);
    cout << "complete" << endl;
    exit(1);*/
    
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
    
    if (GLOBALS.cli.count(CLI_INSTALLSUID)) {
        install(argv[0], true);
        exit(0);
    }
    
    if (GLOBALS.cli.count(CLI_SCHED)) {
        scheduleRun();
        exit(0);
    }
    
    if ((GLOBALS.cli.count(CLI_SCHEDHOUR) || GLOBALS.cli.count(CLI_SCHEDMIN) || GLOBALS.cli.count(CLI_SCHEDPATH)) && !GLOBALS.cli.count(CLI_SCHED)) {
        SCREENERR("error: --schedhour, --schedmin and --schedpath all require --sched")
        exit(1);
    }
    
    if (GLOBALS.cli.count(CLI_VERSION)) {
        cout << "managebackups " << VERSION << "\n";
        cout << "(c) 2023 released under GPLv3." << endl;
        exit(0);
    }
    
    if ((GLOBALS.cli.count(CLI_ALLSEQ) || GLOBALS.cli.count(CLI_ALLPAR) ||
         GLOBALS.cli.count(CLI_CRONS) || GLOBALS.cli.count(CLI_CRONP)) &&
        GLOBALS.cli.count(CLI_PROFILE)) {
        SCREENERR("error: all, cron and profile are mutually-exclusive options");
        exit(1);
    }
    
    if (GLOBALS.cli.count(CLI_PATHS) &&
        (GLOBALS.cli.count(CLI_DIR) || GLOBALS.cli.count(CLI_FAUB) ||
         GLOBALS.cli.count(CLI_ALLSEQ) || GLOBALS.cli.count(CLI_ALLPAR) ||
         GLOBALS.cli.count(CLI_CRONS) || GLOBALS.cli.count(CLI_CRONP) ||
         GLOBALS.cli.count(CLI_PROFILE) || GLOBALS.cli.count(CLI_FILE) || GLOBALS.cli.count(CLI_COMMAND) ||
         GLOBALS.cli.count(CLI_SAVE) || GLOBALS.cli.count(CLI_SCPTO) || GLOBALS.cli.count(CLI_SFTPTO))) {
        SCREENERR("error: --path (or -s) is mutually-exclusive with all other options");
        exit(1);
    }
    
    if ((GLOBALS.cli.count(CLI_ALLSEQ) || GLOBALS.cli.count(CLI_ALLPAR) ||
         GLOBALS.cli.count(CLI_CRONS) || GLOBALS.cli.count(CLI_CRONP)) &&
        (GLOBALS.cli.count(CLI_FILE) || GLOBALS.cli.count(CLI_COMMAND) ||
         GLOBALS.cli.count(CLI_SAVE) || GLOBALS.cli.count(CLI_DIR) ||
         GLOBALS.cli.count(CLI_SCPTO) || GLOBALS.cli.count(CLI_SFTPTO))) {
        SCREENERR(
                  "error: --file, --command, --save, --directory, --scp and --sftp are incompatible with "
                  "--all/--All and --cron/--Cron");
        exit(1);
    }
    
    if ((GLOBALS.cli.count(CLI_FAUB) || GLOBALS.cli.count(CLI_PATHS)) &&
        (GLOBALS.cli.count(CLI_SFTPTO) || GLOBALS.cli.count(CLI_SCPTO))) {
        SCREENERR("error: --faub and --paths are incompatible with --sftp and --scp");
        exit(1);
    }
    
    DEBUG(D_any) DFMT("about to setup config...");
    ConfigManager configManager;
    auto currentConfig = selectOrSetupConfig(configManager, GLOBALS.cli.count(CLI_GO) || GLOBALS.cli.count(CLI_COMPARE) || GLOBALS.cli.count(CLI_COMPAREFILTER)|| GLOBALS.cli.count(CLI_LAST) || GLOBALS.cli.count(CLI_RECALC) || GLOBALS.cli.count(CLI_RELOCATE));
    
    if (currentConfig->modified)
        currentConfig->saveConfig();
    
    if (GLOBALS.cli.count(CLI_RELOCATE)) {
        if (haveProfile(&configManager)) {
            scanConfigToCache(*currentConfig);
            relocateBackups(*currentConfig, GLOBALS.cli[CLI_RELOCATE].as<string>());
        }
        else
            SCREENERR("error: specify a profile to relocate (use -p)");
        exit(1);
    }
    
    auto compareArgs = GLOBALS.cli.count(CLI_COMPARE) + GLOBALS.cli.count(CLI_COMPAREFILTER);
    if (compareArgs || GLOBALS.cli.count(CLI_LAST)) {
        
        if (haveProfile(&configManager)) {
            
            if (!(compareArgs && GLOBALS.cli.count(CLI_LAST))) {
                if (GLOBALS.cli.count(CLI_GO)) {
                    SCREENERR("error: --" << CLI_GO << " is mutually-exclusive with --" << CLI_COMPARE << ", --" << CLI_COMPAREFILTER << ", and --" << CLI_LAST);
                    exit(1);
                }
                    
                if (currentConfig->isFaub()) {
                    scanConfigToCache(*currentConfig);
                    
                    if (GLOBALS.cli.count(CLI_LAST)) {
                        if (!GLOBALS.cli.count(CLI_FORCE) && !GLOBALS.cli.count(CLI_THRESHOLD)) {
                            if (!currentConfig->fcache.displayDiffFiles(""))
                                currentConfig->fcache.compare("", "", GLOBALS.cli.count(CLI_THRESHOLD) ? GLOBALS.cli[CLI_THRESHOLD].as<string>() : "");
                        }
                        else
                            currentConfig->fcache.compare("", "", GLOBALS.cli.count(CLI_THRESHOLD) ? GLOBALS.cli[CLI_THRESHOLD].as<string>() : "");
                    }
                    else {
                        vector<string> v;
                        
                        if (GLOBALS.cli.count(CLI_COMPARE)) {
                            auto v2 = GLOBALS.cli[CLI_COMPARE].as<vector<string>>();
                            v.insert(v.end(), v2.begin(), v2.end());
                        }
                        
                        if (GLOBALS.cli.count(CLI_COMPAREFILTER)) {
                            auto v2 = GLOBALS.cli[CLI_COMPAREFILTER].as<vector<string>>();
                            v.insert(v.end(), v2.begin(), v2.end());
                        }
                        
                        if (compareArgs == 1) {
                            if (!GLOBALS.cli.count(CLI_FORCE) && !GLOBALS.cli.count(CLI_THRESHOLD)) {
                                
                                // single file given, look for cached version of diff
                                // if that fails, fall back to a compare
                                if (!currentConfig->fcache.displayDiffFiles(v[0]))
                                    currentConfig->fcache.compare(v[0], "", GLOBALS.cli.count(CLI_THRESHOLD) ? GLOBALS.cli[CLI_THRESHOLD].as<string>() : "");
                            }
                            else  // --force version: bypass cached version
                                currentConfig->fcache.compare(v[0], "", GLOBALS.cli.count(CLI_THRESHOLD) ? GLOBALS.cli[CLI_THRESHOLD].as<string>() : "");
                        }
                        else
                            if (compareArgs == 2)
                                currentConfig->fcache.compare(v[0], v[1], GLOBALS.cli.count(CLI_THRESHOLD) ? GLOBALS.cli[CLI_THRESHOLD].as<string>() : "");
                            else
                                SCREENERR("error: don't know how to --" << CLI_COMPARE << " more than two backups");
                    }
                    
                    exit(0);
                }
                else {
                    SCREENERR("error: --" << CLI_COMPARE << " is only valid with a faub-backup profile");
                    exit(1);
                }
            }
            else {
                SCREENERR("error: --" << CLI_COMPARE << " & --" << CLI_LAST << " are mutually-exclusive");
                exit(1);
            }
        }
        else {
            SCREENERR("error: --" << CLI_COMPARE << " and --" << CLI_LAST << " are only valid with a profile (use -p)");
            exit(1);
        }
    }
    
    if (GLOBALS.cli.count(CLI_THRESHOLD)) {
        SCREENERR("error: --" << CLI_THRESHOLD << " is only valid in the context of --" << CLI_COMPARE);
        exit(1);
    }
    
    if (GLOBALS.cli.count(CLI_RECALC)) {
        if (!haveProfile(&configManager)) {
            SCREENERR("--recalc requires a profile (use -p)");
            exit(2);
        }
        
        if (!currentConfig->settings[sFaub].value.length()) {
            SCREENERR("--recalc can only be used with a faub-based profile");
            exit(2);
        }
        
        scanConfigToCache(*currentConfig);
        currentConfig->fcache.recache("", 0, true);
        exit(0);
    }
    
    // if displaying stats and --profile hasn't been specified (or matched successfully)
    // then rescan all configs;  otherwise just scan the --profile config
    DEBUG(D_any) DFMT("about to scan directories...");
    
    /* SHOW STATS
     * ****************************/
    if (GLOBALS.stats) {
        if (!currentConfig->temp)
            scanConfigToCache(*currentConfig);
        else
            for (auto &config : configManager.configs)
                if (!config.temp) scanConfigToCache(config);
        
        GLOBALS.cli.count(CLI_STATS1)
        ? displayDetailedStatsWrapper(configManager, (int)GLOBALS.cli.count(CLI_STATS1))
        : displaySummaryStatsWrapper(configManager, (int)GLOBALS.cli.count(CLI_STATS2));
    }
    else {  // "all" profiles locking is handled here; individual profile locking is handled further
        // down in the NORMAL RUN
        if (GLOBALS.cli.count(CLI_ALLSEQ) || GLOBALS.cli.count(CLI_ALLPAR) ||
            GLOBALS.cli.count(CLI_CRONS) || GLOBALS.cli.count(CLI_CRONP)) {
            currentConfig->settings[sDirectory].value = "/";
            currentConfig->settings[sBackupFilename].value = "all";
            
            auto [pid, lockTime] = currentConfig->getLockPID();
            
            if (pid) {
                if (!kill(pid, 0)) {
                    if (GLOBALS.cli.count(CLI_FORCE)) {
                        kill(pid, 15);
                        NOTQUIET && cerr << "[ALL] previous profile lock (running as pid " << pid <<
                        ") released due to --force" << endl;
                        log("[ALL] previous lock (pid " + to_string(pid) + ") released due to --force");
                        GLOBALS.interruptLock = currentConfig->setLockPID(0);
                    }
                    if (GLOBALS.startupTime - lockTime < 60 * 60 * 24) {
                        NOTQUIET &&cerr << "[ALL] profile is locked while previous invocation is "
                        "still running (pid "
                        << pid << "); skipping this run." << endl;
                        log("[ALL] skipped run due to profile lock while previous invocation is "
                            "still running (pid " +
                            to_string(pid) + ")");
                        exit(1);
                    }
                    else
                        kill(pid, 15);
                    notify(*currentConfig,
                           errorcom("ALL",
                                    "abandoning previous lock because its over 24 hours old"),
                           false, true);
                }
                else {
                    log("[ALL] abandoning previous lock because pid " + to_string(pid) +
                        " has vanished");
                }
            }
            
            // locking requested
            if (GLOBALS.cli.count(CLI_LOCK) || GLOBALS.cli.count(CLI_CRONS) ||
                GLOBALS.cli.count(CLI_CRONP))
                GLOBALS.interruptLock = currentConfig->setLockPID(GLOBALS.pid);
        }
        
#define BoolParamIfSpecified(x) (GLOBALS.cli.count(x) ? string(" --") + x : "")
#define ValueParamIfSpecified(x) \
(GLOBALS.cli.count(x) ? " " + currentConfig->settings[settingMap[x]].execParam : "")
        
        string commonSwitches =
        string(NOTQUIET ? "" : " -q") + BoolParamIfSpecified(CLI_TEST) +
        BoolParamIfSpecified(CLI_NOBACKUP) + BoolParamIfSpecified(CLI_NOPRUNE) +
        BoolParamIfSpecified(CLI_PRUNE) + BoolParamIfSpecified(CLI_FILTERDIRS) +
        (GLOBALS.cli.count(CLI_CONFDIR) ? string("--") + CLI_CONFDIR + " '" + GLOBALS.confDir + "'" : "") +
        (GLOBALS.cli.count(CLI_CACHEDIR) ? string("--") + CLI_CACHEDIR + " '" + GLOBALS.cacheDir + "'" : "") +
        (GLOBALS.cli.count(CLI_LOGDIR) ? string("--") + CLI_LOGDIR + " '" + GLOBALS.logDir + "'" : "") +
        ValueParamIfSpecified(CLI_FAUB) + ValueParamIfSpecified(CLI_PATHS) +
        ValueParamIfSpecified(CLI_FS_FP) + ValueParamIfSpecified(CLI_FS_BACKUPS) +
        ValueParamIfSpecified(CLI_FS_SLOW) +
        ValueParamIfSpecified(CLI_FS_DAYS) + ValueParamIfSpecified(CLI_TIME) +
        ValueParamIfSpecified(CLI_MODE) + ValueParamIfSpecified(CLI_MINSPACE) +
        ValueParamIfSpecified(CLI_MINSFTPSPACE) + ValueParamIfSpecified(CLI_DOW) +
        ValueParamIfSpecified(CLI_NOCOLOR) + ValueParamIfSpecified(CLI_NOTIFY) +
        ValueParamIfSpecified(CLI_NOS) + ValueParamIfSpecified(CLI_DAYS) +
        ValueParamIfSpecified(CLI_WEEKS) + ValueParamIfSpecified(CLI_MONTHS) +
        ValueParamIfSpecified(CLI_TRIPWIRE) + ValueParamIfSpecified(CLI_NOTIFYEVERY) +
        ValueParamIfSpecified(CLI_YEARS) + ValueParamIfSpecified(CLI_NICE) +
        ValueParamIfSpecified(CLI_INCLUDE) + ValueParamIfSpecified(CLI_EXCLUDE) +
        (GLOBALS.cli.count(CLI_LOCK) || GLOBALS.cli.count(CLI_CRONS) || GLOBALS.cli.count(CLI_CRONP) ? " -x" : "") +
        ValueParamIfSpecified(CLI_MAXLINKS);
        
        if (GLOBALS.debugSelector) commonSwitches += " -v=" + to_string(GLOBALS.debugSelector);
        
        try {
            if (GLOBALS.cli.count(CLI_PATHS)) {
                // get the list of --path "foo" --path "bar" parameters that are set
                auto paths = GLOBALS.cli[CLI_PATHS].as<vector<string>>();
                vector<string> newPaths;
                
                // loop through to see if any are space-delimited containing multiple paths in one
                for (auto &p: paths) {
                    Pcre pathRE("((?:([\'\"])(?:(?!\\g2).|(?:(?<=\\\\)[\'\"]))+(?<!\\\\)\\g2)|(?:\\S|(?:(?<=\\\\)\\s))+)", "g");
                    size_t pos = 0;
                    string match;
                    
                    while (pos <= p.length() && pathRE.search(p, (int)pos)) {
                        pos = pathRE.get_match_end(0);
                        ++pos;
                        match = pathRE.get_match(0);
                        
                        if (match.length())
                            newPaths.insert(newPaths.end(), trimQuotes(match));
                    }
                    
                }
                
                paths = newPaths;
                
                // start faub client-side
                fc_mainEngine(paths);
                exit(1);
            }
            
            /* ALL SEQUENTIAL RUN (prune, link, backup)
             * for all profiles
             * ****************************/
            if (GLOBALS.cli.count(CLI_ALLSEQ) || GLOBALS.cli.count(CLI_CRONS)) {
                timer allRunTimer;
                allRunTimer.start();
                log("[ALL] starting sequential processing of all profiles");
                NOTQUIET && cout << "starting sequential processing of all profiles" << endl;
                                
                for (auto &config : configManager.configs) {
                   if (!config.temp) {
                       NOTQUIET &&cout << "\n"
                       << BOLDBLUE << "[" << config.settings[sTitle].value << "]"
                       << RESET << "\n";
                       PipeExec miniMe(string(argv[0]) + " -p " + config.settings[sTitle].value +
                                        commonSwitches);
                       miniMe.execute("", true, false, true);   // fds closed and kids piped up via destructor
                    }
                }
                
                allRunTimer.stop();
                NOTQUIET &&cout << "\ncompleted sequential processing of all profiles in "
                << allRunTimer.elapsed() << endl;
                log("[ALL] completed sequential processing of all profiles in " +
                    allRunTimer.elapsed());
            }
            
            /* ALL PARALLEL RUN (prune, link, backup)
             * ****************************/
            else if (GLOBALS.cli.count(CLI_ALLPAR) || GLOBALS.cli.count(CLI_CRONP)) {
                timer allRunTimer;
                
                allRunTimer.start();
                log("[ALL] starting parallel processing of all profiles");
                NOTQUIET &&cout << "starting parallel processing of all profiles" << endl;
                
                map<int, PipeExec> childProcMap;
                for (auto &config : configManager.configs)
                    if (!config.temp) {
                        // launch each profile in a separate child
                        PipeExec miniMe(string(argv[0]) + " -p " + config.settings[sTitle].value +
                                        commonSwitches + " -z");
                        auto childPID = miniMe.execute("", true, true, true);
                        miniMe.closeAll();
                        
                        // save the child PID and pipe object in our map
                        // emplace() into the map that's outside this loop keeps the destructor
                        // from being called yet and blocking our execution with a wait().
                        childProcMap.emplace(pair<int, PipeExec>(childPID, miniMe));
                    }
                
                // wait while all child procs finish
                while (childProcMap.size()) {
                    int pid = wait(NULL);
                    
                    if (pid > 0 && childProcMap.find(pid) != childProcMap.end())
                        childProcMap.erase(pid);
                    
                    if (pid == -1) {
                        NOTQUIET &&cout << "[ALL] aborting on error: wait() returned "
                        << to_string(errno) << endl;
                        log("[ALL] aborting on error: wait() returned " + to_string(errno));
                        break;
                    }
                }
                
                allRunTimer.stop();
                NOTQUIET &&cout << "completed parallel processing of all profiles in "
                << allRunTimer.elapsed() << endl;
                log("[ALL] completed parallel processing of all profiles in " +
                    allRunTimer.elapsed());
            }
            
            /* NORMAL RUN (prune, link, backup)
             * ****************************/
            else {
                auto [pid, lockTime] = currentConfig->getLockPID();
                
                if (pid) {
                    if (!kill(pid, 0)) {
                        if (GLOBALS.cli.count(CLI_FORCE)) {
                            kill(pid, 15);
                            NOTQUIET && cerr << currentConfig->ifTitle() << " previous profile lock (running as pid " << pid <<
                            ") released due to --force" << endl;
                            log("[ALL] previous lock (pid " + to_string(pid) + ") released due to --force");
                            GLOBALS.interruptLock = currentConfig->setLockPID(0);
                        }
                        if (GLOBALS.startupTime - lockTime < 60 * 60 * 24) {
                            NOTQUIET &&cerr << currentConfig->ifTitle() <<
                            " profile is locked while previous invocation is still running (pid "
                            << pid << "); skipping this run." << endl;
                            log(currentConfig->ifTitle() +
                                " skipped run due to profile lock while previous invocation is "
                                "still running (pid " +
                                to_string(pid) + ")");
                            exit(1);
                        }
                        else {
                            notify(
                                   *currentConfig,
                                   errorcom(currentConfig->ifTitle(),
                                            "abandoning previous lock because its over 24 hours old"),
                                   false, true);
                            kill(pid, 15);
                        }
                    }
                    else
                        log(currentConfig->ifTitle() + " abandoning previous lock because pid " +
                            to_string(pid) + " has vanished");
                }
                
                if (GLOBALS.cli.count(CLI_LOCK) || GLOBALS.cli.count(CLI_CRONS) ||
                    GLOBALS.cli.count(CLI_CRONP))
                    GLOBALS.interruptLock = currentConfig->setLockPID(GLOBALS.pid);
                
                int n = nice(0);
                if (currentConfig->settings[sNice].ivalue() != n) {
                    errno = 0;
                    int newnice = nice(currentConfig->settings[sNice].ivalue());
                    DEBUG(D_any) DFMT("set nice value of " << currentConfig->settings[sNice].ivalue() << " (previous " << n << ", got " << newnice << ")");
                    if (newnice == -1 && errno)
                        log(currentConfig->ifTitle() + " unable to set nice value" + errtext());
                }
                
                scanConfigToCache(*currentConfig);
                
                if (performTripwire(*currentConfig)) {
                    
                    /* faub configurations */
                    if (currentConfig->isFaub()) {
                        if (shouldPrune(*currentConfig))
                            pruneBackups(*currentConfig);
                        
                        fs_startServer(*currentConfig);
                    }
                    else {  /* single file configurations */
                        if (shouldPrune(*currentConfig))
                            pruneBackups(*currentConfig);
                        
                        updateLinks(*currentConfig);
                        
                        if (enoughLocalSpace(*currentConfig)) {
                            performBackup(*currentConfig);
                        }
                    }
                    
                    configManager.housekeeping();
                }
            }
        }
        catch (MBException &e) {
            log("aborting due to " + e.detail());
            cleanupAndExitOnError();
        }
        
        DEBUG(D_any) DFMT("completed primary tasks");
        
        // remove lock
        if (GLOBALS.cli.count(CLI_LOCK) || GLOBALS.cli.count(CLI_CRONS) ||
            GLOBALS.cli.count(CLI_CRONP))
            GLOBALS.interruptLock = currentConfig->setLockPID(0);
    }
    
    AppTimer.stop();
    DEBUG(D_any)
    DFMT("stats: " << BOLDGREEN << GLOBALS.statsCount << RESET << GREEN << ", md5s: " << BOLDGREEN
         << GLOBALS.md5Count << RESET << GREEN << ", total time: " << BOLDGREEN
         << AppTimer.elapsed(3) << YELLOW << " (current run)");
    
    unsigned long oldStats;
    unsigned long oldMd5s;
    string oldElapsed;
    
    if (getGlobalStats(oldStats, oldMd5s, oldElapsed)) {
        DEBUG(D_any)
        DFMT("stats: " << BOLDGREEN << oldStats << RESET << GREEN << ", md5s: " << BOLDGREEN
             << oldMd5s << RESET << GREEN << ", total time: " << BOLDGREEN
             << oldElapsed << YELLOW << " (previous run)");
    }
    
    saveGlobalStats(GLOBALS.statsCount, GLOBALS.md5Count, AppTimer.elapsed(3));
    
    return 0;
}
