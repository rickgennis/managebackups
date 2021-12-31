#include "BackupEntry.h"
#include "BackupCache.h"
#include "BackupConfig.h"
#include "ConfigManager.h"
#include "cxxopts.hpp"

#include "syslog.h"
#include "unistd.h"
#include <sys/stat.h>
#include <pcre++.h>
#include <iostream>
#include <filesystem>
#include <dirent.h>
#include <time.h>
#include "globalsdef.h"
#include "colors.h"
#include "statistics.h"
#include "util.h"


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
                        DEBUG(2) && cout << "skipping due to name mismatch: " << fullFilename << endl;
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
            fnamePattern = regEx.get_match(0) + "-20\\d{2}[-.]*\\d{2}[-.]*\\d{2}.*\\." + regEx.get_match(1);
    }
    else 
        fnamePattern = ".*-20\\d{2}[-.]*\\d{2}[-.]*\\d{2}.*";

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
            cerr << ifcolor(RED) << "error: title not found; try -1 or -2 with no title to see all backups" << ifcolor(RESET) << endl;
            exit(1);
        }

        currentConf->loadConfigsCache();
    }
    else {
        if (bSave) {
            cerr << ifcolor(RED) << "error: --title must be specified in order to --save settings" << ifcolor(RESET) << endl;
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
                cerr << "SETTING BOOL: " << setting_it->value << endl;
                break;
            }
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
        cerr << ifcolor(RED) << "error: --directory is required" << ifcolor(RESET) << endl;
        exit(1);
    }

    return(currentConf);
}




void pruneBackups(BackupConfig& config) {
    Pcre regEnabled("^(t|true|y|yes|1)$", "i");

    if (!config.settings[sPruneLive].value.length() || !regEnabled.search(config.settings[sPruneLive].value)) {
        cout << ifcolor(RED) << "warning: While a core feature, managebackups doesn't prune old backups" << endl;
        cout << "until specifically enabled.  Use --prune to enable pruning.  Use --prune" << endl;
        cout << "and --save to make it the default behavior for this backup config." << endl;
        cout << "pruning skipped;  would have used these settings:" << endl;
        cout << "\t--days " << config.settings[sDays].value << " --weeks " << config.settings[sWeeks].value;
        cout << " --months " << config.settings[sMonths].value << " --years " << config.settings[sYears].value << ifcolor(RESET) << endl;
        return;
    }


}


int main(int argc, char *argv[]) {
    GLOBALS.statsCount = 0;
    GLOBALS.md5Count = 0;
    GLOBALS.pid = getpid();

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
        (string(CLI_SAVE), "Save config", cxxopts::value<bool>()->default_value("false"))
        (CLI_FS_BACKUPS, "Failsafe Backups", cxxopts::value<int>())
        (CLI_FS_DAYS, "Failsafe Days", cxxopts::value<int>())
        (CLI_DIR, "Directory", cxxopts::value<std::string>())
        (CLI_COPYTO, "Copy to", cxxopts::value<std::string>())
        (CLI_SFTPTO, "SFTP to", cxxopts::value<std::string>())
        (CLI_STATS1, "Stats summary", cxxopts::value<bool>()->default_value("false"))
        (CLI_STATS2, "Stats detail", cxxopts::value<bool>()->default_value("false"))
        (CLI_PRUNE, "Enable pruning", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        (CLI_NOCOLOR, "Disable color", cxxopts::value<bool>()->default_value("false"));

    try {
        GLOBALS.cli = options.parse(argc, argv);
        GLOBALS.debugLevel = GLOBALS.cli.count("verbose");
        GLOBALS.color = !GLOBALS.cli[CLI_NOCOLOR].as<bool>();
    }
    catch (cxxopts::OptionParseException& e) {
        cerr << "managebackups: " << e.what() << endl;
        exit(1);
    }

    ConfigManager configManager;
    auto currentConfig = selectOrSetupConfig(configManager);

    if (GLOBALS.cli.count(CLI_STATS1) || GLOBALS.cli.count(CLI_STATS2)) {
        displayStats(configManager);
        exit(0);
    }

    pruneBackups(*currentConfig);

    //scanConfigToCache(*currentConfig);
    //cout << currentConfig->cache.fullDump() << endl;

    cout << "stats: " << GLOBALS.statsCount << ", md5s: " << GLOBALS.md5Count << endl;
    return 0;
}
