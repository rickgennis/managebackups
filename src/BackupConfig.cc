#include <fstream>
#include <sys/stat.h>
#include <stdio.h>
#include <variant>

#include "BackupConfig.h"
#include "Setting.h"
#include "util.h"
#include "globals.h"
#include <pcre++.h>

using namespace pcrepp;

// conf file regexes
#define CAPTURE_VALUE string("((?:\\s|=|:)+)(.*?)\\s*?")
#define RE_COMMENT "((?:\\s*#|//).*)*$"
#define RE_BLANK "^((?:\\s*#|//).*)*$"
#define RE_TITLE "(title)"
#define RE_DIR "(dir|directory)"
#define RE_FILE "(file|filename)"
#define RE_CMD "(command|cmd)"
#define RE_CP "(copy|copyto|copy_to|cp)"
#define RE_SFTP "(sftp|sftp_to|sftpto)"
#define RE_DAYS "(daily|dailies|days)"
#define RE_WEEKS "(weekly|weeklies|weeks)"
#define RE_MONTHS "(monthly|monthlies|months)"
#define RE_YEARS "(yearly|yearlies|years)"
#define RE_FSBACKS "(failsafe_backups)"
#define RE_FSDAYS "(failsafe_days)"
#define RE_NOTIFY "(notify)"


BackupConfig::BackupConfig() {
    modified = 0;
    config_filename = "";

    // define settings and their defaults
    // order of these inserts matter because they're accessed by position via the SetSpecifier enum
    settings.insert(settings.end(), Setting("title", RE_TITLE, STRING, ""));
    settings.insert(settings.end(), Setting("directory", RE_DIR, STRING, ""));
    settings.insert(settings.end(), Setting("filename", RE_FILE, STRING, ""));
    settings.insert(settings.end(), Setting("command", RE_CMD, STRING, ""));
    settings.insert(settings.end(), Setting("copyto", RE_CP, STRING, ""));
    settings.insert(settings.end(), Setting("sftpto", RE_SFTP, STRING, ""));
    settings.insert(settings.end(), Setting("days", RE_DAYS, INT, 14));
    settings.insert(settings.end(), Setting("weeks", RE_WEEKS, INT, 4));
    settings.insert(settings.end(), Setting("months", RE_MONTHS, INT, 6));
    settings.insert(settings.end(), Setting("years", RE_YEARS, INT, 2));
    settings.insert(settings.end(), Setting("failsafe_backups", RE_FSBACKS, INT, 0));
    settings.insert(settings.end(), Setting("failsafe_days", RE_FSDAYS, INT, 0));
    settings.insert(settings.end(), Setting("notify", RE_NOTIFY, VECTOR, ""));
}


BackupConfig::~BackupConfig() {
    if (modified)
        saveConfig();

    modified = 0;
}


void BackupConfig::saveConfig() {
    ifstream oldFile;
    ofstream newFile;

    // construct a unique config filename if not already specified
    if (!config_filename.length())  {
        string baseName = settings[sTitle].getValue().length() ? settings[sTitle].getValue() : "default"; 
        if (stat((string(CONF_DIR) + baseName + ".conf").c_str(), NULL)) {
            int suffix = 1;
            while (stat((string(CONF_DIR) + baseName + to_string(suffix) + ".conf").c_str(), NULL)) 
                ++suffix;

            config_filename = string(CONF_DIR) + baseName + to_string(suffix) + ".conf";
        }
    }

    // open existing and new config files
    string temp_filename = config_filename + ".tmp." + to_string(GLOBALS.pid);
    oldFile.open(config_filename);
    newFile.open(temp_filename);

    if (!newFile.is_open()) {
        cerr << "error: unable to create " << temp_filename << " (directory not writable?)" << endl;
        log("error: unable to create " + temp_filename + " (directory not writable?)");
        exit(1);
    }

    if (oldFile.is_open()) {
        Pcre reBlank(RE_BLANK);

        string dataLine;
        unsigned int line = 0;

        try {
            string delimiter = ": ";

            // loop through lines of the existing config file
            while (getline(oldFile, dataLine)) {
                ++line;

                // compare the line against each of the config settings until there's a match
                bool identified = false;
                if (!reBlank.search(dataLine)) {
                    for (auto cfg_it = settings.begin(); cfg_it != settings.end(); ++cfg_it) {
                        if (cfg_it->regex.search(dataLine) && cfg_it->regex.matches() > 2) {
                            delimiter = cfg_it->regex.get_match(1);  // save user's preferred delimiter
                            newFile << cfg_it->regex.get_match(0) << cfg_it->regex.get_match(1) << cfg_it->getValue() << 
                                (cfg_it->regex.matches() > 3 ? cfg_it->regex.get_match(3) : "")  << endl;
                            identified = true;
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

            // loop through settings that weren't specified in the existing config;
            // if any of the current value (likely specified via command line parameters on startup)
            // differ from the defaults, write them to the new file.
            for (auto cfg_it = settings.begin(); cfg_it != settings.end(); ++cfg_it)
                if (!cfg_it->seen && (cfg_it->getValue() != cfg_it->getValue(1))) {
                    newFile << cfg_it->display_name << delimiter << cfg_it->getValue() << endl;
                }

            oldFile.close();
            newFile.close();
            remove(config_filename.c_str());
            rename(temp_filename.c_str(), config_filename.c_str());
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
    }
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
                        cfg_it->setValue(cfg_it->regex.get_match(2));
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
        DEBUG(1) && cout << "successfully parsed config " << filename << endl;
        return 1;
    }    

    return 0;
}

