#include <fstream>
#include <sys/stat.h>
#include <stdio.h>
#include <variant>
#include <unistd.h>
#include <dirent.h>

#include "BackupConfig.h"
#include "Setting.h"
#include "util_generic.h"
#include "globals.h"
#include "colors.h"
#include <pcre++.h>

using namespace pcrepp;


BackupConfig::BackupConfig(bool makeTemp) {
    modified = 0;
    temp = makeTemp;
    config_filename = "";

    // define settings and their defaults
    // order of these inserts matter because they're accessed by position via the SetSpecifier enum
    settings.insert(settings.end(), Setting(CLI_TITLE, RE_TITLE, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_DIR, RE_DIR, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_FILE, RE_FILE, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_COMMAND, RE_CMD, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_DAYS, RE_DAYS, INT, "14"));
    settings.insert(settings.end(), Setting(CLI_WEEKS, RE_WEEKS, INT, "4"));
    settings.insert(settings.end(), Setting(CLI_MONTHS, RE_MONTHS, INT, "6"));
    settings.insert(settings.end(), Setting(CLI_YEARS, RE_YEARS, INT, "2"));
    settings.insert(settings.end(), Setting(CLI_FS_BACKUPS, RE_FSBACKS, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_FS_DAYS, RE_FSDAYS, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_COPYTO, RE_CP, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_SFTPTO, RE_SFTP, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_PRUNE, RE_PRUNE, BOOL, "0"));
    settings.insert(settings.end(), Setting(CLI_NOTIFY, RE_NOTIFY, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_MAXLINKS, RE_MAXLINKS, INT, "20"));
    settings.insert(settings.end(), Setting(CLI_TIME, RE_TIME, BOOL, "0"));
}


BackupConfig::~BackupConfig() {
    if (modified)
        saveConfig();
}


void BackupConfig::saveConfig() {
    ifstream oldFile;
    ofstream newFile;

    // don't save temp configs
    if (temp)
        return;

    // construct a unique config filename if not already specified
    if (!config_filename.length()) {
        string baseName = settings[sTitle].value.length() ? settings[sTitle].value : "default"; 
        struct stat statBuf;
        if (stat((string(CONF_DIR) + baseName + ".conf").c_str(), &statBuf)) {
            int suffix = 1;
            while (stat((string(CONF_DIR) + baseName + to_string(suffix) + ".conf").c_str(), &statBuf)) 
                ++suffix;

            config_filename = string(CONF_DIR) + baseName + to_string(suffix) + ".conf";
        }
    }

    // open existing and new config files
    string temp_filename = config_filename + ".tmp." + to_string(GLOBALS.pid);
    oldFile.open(config_filename);
    newFile.open(temp_filename);

    if (!newFile.is_open()) {
        cerr << "error: unable to create " << temp_filename << " (directory not writable?)" << endl << endl;
        cerr << "When --save is specified managebackups writes its configs to " << CONF_DIR << "." << endl;
        cerr << "An initial --save run via sudo is sufficient then leave --save off of subsequent runs." << endl;
        cerr << "However, managebackups will always need write acces to " << CONF_DIR << "/caches." << endl;
        log("error: unable to create " + temp_filename + " (directory not writable?)");
        exit(1);
    }

    string usersDelimiter = ": ";
    if (oldFile.is_open()) {
        Pcre reBlank(RE_BLANK);

        string dataLine;
        unsigned int line = 0;

        try {
            // loop through lines of the existing config file
            while (getline(oldFile, dataLine)) {
                ++line;

                // compare the line against each of the config settings until there's a match
                bool identified = false;
                if (!reBlank.search(dataLine)) {
                    for (auto set_it = settings.begin(); set_it != settings.end(); ++set_it) {
                        if (set_it->regex.search(dataLine) && set_it->regex.matches() > 2) {
                            usersDelimiter = set_it->regex.get_match(1);
                            newFile << set_it->regex.get_match(0) << set_it->regex.get_match(1) << set_it->value << 
                                (set_it->regex.matches() > 3 ? set_it->regex.get_match(3) : "")  << endl;
                            set_it->seen = identified = true;
                            break;
                        }
                    }

                    // comment out unrecognized settings
                    if (!identified) { 
                        newFile << "// " << dataLine << "       # unknown setting?" << endl; 
                        continue;
                    }
                }

                // add the line as is (likely a comment or blank line)
                if (!identified)
                    newFile << dataLine << endl;
            }
        }
        catch (...) {
            oldFile.close();
            newFile.close();
            remove(temp_filename.c_str());
            log("error: unable to process the directive on line " + to_string(line) + " of " + config_filename);
            cerr << "error: unable to process the directive on line " << line << " of " << config_filename << endl;
            cerr << "    " << dataLine << endl;
            exit(1);
        }

        oldFile.close();
    }

    // loop through settings that weren't specified in the existing config;
    // if any of the current values (likely specified via command line parameters on startup)
    // differ from the defaults, write them to the new file.
    for (auto cfg_it = settings.begin(); cfg_it != settings.end(); ++cfg_it)
        if (!cfg_it->seen && (cfg_it->value != cfg_it->defaultValue)) {
            newFile << cfg_it->display_name << usersDelimiter << cfg_it->value << endl;
        }

    newFile.close();
    remove(config_filename.c_str());
    rename(temp_filename.c_str(), config_filename.c_str());
}


bool BackupConfig::loadConfig(string filename) {
    ifstream configFile;

    configFile.open(filename);
    if (configFile.is_open()) {
        string dataLine;

        Pcre reBlank(RE_BLANK);
        config_filename = filename;

        unsigned int line = 0;
        try {
            while (getline(configFile, dataLine)) {
                ++line;

                // compare the line against each of the config settings until there's a match
                bool identified = false;
                for (auto cfg_it = settings.begin(); cfg_it != settings.end(); ++cfg_it) {
                    if (cfg_it->regex.search(dataLine) && cfg_it->regex.matches() > 2) {
                        cfg_it->value = cfg_it->regex.get_match(2);
                        identified = true;
                        break;
                    }
                }

                if (!identified && !reBlank.search(dataLine)) {
                    cout << "error: unrecognized setting on line " << line << " of " << filename << endl;
                    cout << "    " << dataLine << endl; 
                    log("error: unrecognized setting on line " + to_string(line) + " of " + filename);
                    exit(1);
                }
            }   // while
        }
        catch (...) {
            configFile.close();
            log("error: unable to parse a numeric value for the directive on line " + to_string(line) + " of " + filename);
            cerr << "error: unable to parse a numeric value for the directive on line " << line << " of " << filename << endl;
            cerr << "    " << dataLine << endl;
            exit(1);
        }
            
        configFile.close();
        DEBUG(1, "successfully parsed [" << settings[sTitle].value << "] config from " << filename);

        return 1;
    }    

    return 0;
}


void BackupConfig::loadConfigsCache() {
    cache.cacheFilename = string(CONF_DIR) + "/caches/" + MD5string(settings[sDirectory].value + settings[sBackupFilename].value);
    struct stat statBuf;

    if (!stat(cache.cacheFilename.c_str(), &statBuf)) {
        cache.restoreCache();
    }
    else {
        mkdir((string(CONF_DIR) + "/caches").c_str(),  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }
}


void BackupConfig::fullDump() {
    for (auto set_it = settings.begin(); set_it != settings.end(); ++set_it)
        cout << "setting " << set_it->display_name << ": " << set_it->value << endl;
}


unsigned int BackupConfig::removeEmptyDirs(string directory) {
    DIR *c_dir;
    struct dirent *c_dirEntry;

    string dir = directory.length() ? directory : settings[sDirectory].value;
    if ((c_dir = opendir(dir.c_str())) != NULL) {
        unsigned int entryCount = 0;

        while ((c_dirEntry = readdir(c_dir)) != NULL) {

            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
               continue; 

            ++entryCount;
            ++GLOBALS.statsCount;
            struct stat statData;
            string fullFilename = addSlash(dir) + string(c_dirEntry->d_name);
            if (!stat(fullFilename.c_str(), &statData)) {

                // recurse into subdirectories
                if ((statData.st_mode & S_IFMT) == S_IFDIR) {
                    if (!removeEmptyDirs(fullFilename)) {
                        NOTQUIET && cout << "[" << settings[sTitle].value + "] removing empty directory " << fullFilename << endl;
                        log("[" + settings[sTitle].value + "] removing empty directory " + fullFilename);
                        rmdir(fullFilename.c_str());    // remove empty subdirectory
                        --entryCount;
                    }
                }
            }
        }

        closedir(c_dir);
        return entryCount;
    }

    return 1;
}

