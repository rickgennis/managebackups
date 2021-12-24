#include <fstream>
#include <sys/stat.h>
#include <stdio.h>

#include "BackupConfig.h"
#include "util.h"
#include "globals.h"
#include <pcre++.h>

using namespace pcrepp;

// conf file regexes
// ((?:\s|=|:)+)(.*?)\s*((?:#|//).*)*$


#define CAPTURE_VALUE string("((?:\\s|=|:)+)(.*?)\\s*")
#define RE_COMMENT "\\s*((?:#|//).*)*$"
#define RE_BLANK "^\\s*$"
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

// define CAPTURE_VALUE string("(\\s|=|:)+([^#]+)")
// define RE_COMMENT "(\\s*(#|//).*)"

class Config_Setting {
    public:
        Pcre regex;
        bool is_numeric;
        bool seen;
        void *variable;

        string newValue();
        Config_Setting(string pattern, bool numeric, void *realData);
}; 


Config_Setting::Config_Setting(string pattern, bool numeric, void *realData) {
    regex = Pcre(pattern + CAPTURE_VALUE + RE_COMMENT + "*");
    is_numeric = numeric;
    seen = false;
    variable = realData;
}


string Config_Setting::newValue() {
    seen = true;
    return(is_numeric ? to_string(*(unsigned int*)variable) : *(string*)variable);
}


BackupConfig::BackupConfig() {
    title = directory = backup_filename = backup_command = cp_to = sftp_to = "";
    failsafe_backups = failsafe_days = 0;
    default_days = 14;
    default_weeks = 4;
    default_months = 6;
    default_years = 1;
    modified = 0;
    config_filename = "";
}


BackupConfig::~BackupConfig() {
    if (modified)
        saveConfig();

    cout << "SAVING CONFIG" << endl;
    modified = 0;
}


void BackupConfig::saveConfig() {
    ifstream oldFile;
    ofstream newFile;

    // construct a unique config filename if not already specified
    if (!config_filename.length())  {
        string baseName = title.length() ? title : "default";
        if (stat((string(CONF_DIR) + baseName + ".conf").c_str(), NULL)) {
            int suffix = 1;
            while (stat((string(CONF_DIR) + baseName + to_string(suffix) + ".conf").c_str(), NULL)) 
                ++suffix;

            config_filename = string(CONF_DIR) + baseName + to_string(suffix) + ".conf";
        }
    }

    // open existing and new config files
    string temp_filename = config_filename + ".tmp." + to_string(g_pid);
    oldFile.open(config_filename);
    newFile.open(temp_filename);

    if (!newFile.is_open()) {
        cerr << "error: unable to create " << temp_filename << " (directory not writable?)" << endl;
        log("error: unable to create " + temp_filename + " (directory not writable?)");
        exit(1);
    }

    if (oldFile.is_open()) {
        // precompile regexes and map settings to class variables
        vector<Config_Setting> settings;
        settings.insert(settings.begin(), Config_Setting(RE_TITLE, 0, &title));
        settings.insert(settings.begin(), Config_Setting(RE_DIR, 0, &directory));
        settings.insert(settings.begin(), Config_Setting(RE_FILE, 0, &backup_filename));
        settings.insert(settings.begin(), Config_Setting(RE_CMD, 0, &backup_command));
        settings.insert(settings.begin(), Config_Setting(RE_CP, 0, &cp_to));
        settings.insert(settings.begin(), Config_Setting(RE_SFTP, 0, &sftp_to));
        settings.insert(settings.begin(), Config_Setting(RE_DAYS, 1, &default_days));
        settings.insert(settings.begin(), Config_Setting(RE_WEEKS, 1, &default_weeks));
        settings.insert(settings.begin(), Config_Setting(RE_MONTHS, 1, &default_months));
        settings.insert(settings.begin(), Config_Setting(RE_YEARS, 1, &default_years));
        settings.insert(settings.begin(), Config_Setting(RE_FSBACKS, 1, &failsafe_backups));
        settings.insert(settings.begin(), Config_Setting(RE_FSDAYS, 1, &failsafe_days));
        Pcre reBlank(RE_BLANK);

        string dataLine;
        unsigned int line = 0;

        try {
            // loop through lines of the existing config file
            while (getline(oldFile, dataLine)) {
                ++line;

                // compare the line against each of the config settings until there's a match
                bool identified = false;
                for (auto cfg_it = settings.begin(); cfg_it != settings.end(); ++cfg_it) {
                    cerr << dataLine << endl;
                    if (!cfg_it->regex.search(dataLine))
                        continue;
                    cerr << "search: " << cfg_it->regex.search(dataLine) << ", matches: " << cfg_it->regex.matches() << endl;
                    cerr << "0[" << cfg_it->regex.get_match(0) << "]" << endl;
                    cerr << "1[" << cfg_it->regex.get_match(1) << "]" << endl;
                    cerr << "2[" << cfg_it->regex.get_match(2) << "]" << endl;
                    cerr << "3[" << cfg_it->regex.get_match(3) << "]" << endl;
                    if (cfg_it->regex.search(dataLine) && cfg_it->regex.matches() > 3) {
                        newFile << cfg_it->regex.get_match(0) + cfg_it->regex.get_match(1) + cfg_it->newValue() + cfg_it->regex.get_match(3) << endl;
                        identified = true;
                        break;
                    }
                }

                // comment out unrecognized settings
                if (!identified && !reBlank.search(dataLine)) {
                    newFile << "// " << dataLine << "       # unknown setting?" << endl; 
                }

/*                if (reNotify.search(dataLine) && reNotify.matches()) {
                    Pcre reDelimiters("\\s*[,;]\\s*", "g");
                    auto notifies = reDelimiters.split(reNotify.get_match(2));
                    for (auto not_it = notifies.begin(); not_it != notifies.end(); ++not_it)
                        notify.insert(*not_it);
                  }*/
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
    }
}


bool BackupConfig::loadConfig(string filename) {
    ifstream configFile;

    configFile.open(filename);
    if (configFile.is_open()) {
        string dataLine;

        // precompile regexes
        Pcre removeComments(RE_COMMENT);
        Pcre reBlank(RE_BLANK);
        Pcre reTitle(RE_TITLE + CAPTURE_VALUE);
        Pcre reDirectory(RE_DIR + CAPTURE_VALUE);
        Pcre reFilename(RE_FILE + CAPTURE_VALUE);
        Pcre reCommand(RE_CMD + CAPTURE_VALUE);
        Pcre reCP(RE_CP + CAPTURE_VALUE);
        Pcre reSFTP(RE_SFTP + CAPTURE_VALUE);
        Pcre reDays(RE_DAYS + CAPTURE_VALUE + RE_COMMENT);
        Pcre reWeeks(RE_WEEKS + CAPTURE_VALUE);
        Pcre reMonths(RE_MONTHS + CAPTURE_VALUE);
        Pcre reYears(RE_YEARS + CAPTURE_VALUE);
        Pcre reFailsafeBackups(RE_FSBACKS + CAPTURE_VALUE);
        Pcre reFailsafeDays(RE_FSDAYS + CAPTURE_VALUE);
        Pcre reNotify(RE_NOTIFY + CAPTURE_VALUE);

        config_filename = filename;

        unsigned int line = 0;
        try {
            while (getline(configFile, dataLine)) {
                ++line;

                // remove comments
                dataLine = removeComments.replace(dataLine, "");

                // match against specific directives
                if (reTitle.search(dataLine) && reTitle.matches())
                    title = reTitle.get_match(2);
                else    // directory
                    if (reDirectory.search(dataLine) && reDirectory.matches())
                        directory = reDirectory.get_match(2);
                else    // filename
                    if (reFilename.search(dataLine) && reFilename.matches())
                        backup_filename = reFilename.get_match(2);
                else    // command
                    if (reCommand.search(dataLine) && reCommand.matches())
                        backup_command = reCommand.get_match(2);
                else    // copyto
                    if (reCP.search(dataLine) && reCP.matches())
                        cp_to = reCP.get_match(2);
                else    // sftp
                    if (reSFTP.search(dataLine) && reSFTP.matches())
                        sftp_to = reSFTP.get_match(2);
                else    // days
                    if (reDays.search(dataLine) && reDays.matches()) {
                        cout << "matches = " << reDays.matches() << "; Match = " << reDays.get_match(2) << endl;
                        continue;
                        default_days = stoi(reDays.get_match(2));
                    }
                else    // weeks
                    if (reWeeks.search(dataLine) && reWeeks.matches())
                        default_weeks = stoi(reWeeks.get_match(2));
                else    // months
                    if (reMonths.search(dataLine) && reMonths.matches())
                        default_months = stoi(reMonths.get_match(2));
                else    // years
                    if (reYears.search(dataLine) && reYears.matches())
                        default_years = stoi(reYears.get_match(2));
                else    // failsafe_backups 
                    if (reFailsafeBackups.search(dataLine) && reFailsafeBackups.matches())
                        failsafe_backups = stoi(reFailsafeBackups.get_match(2));
                else    // failsafe_days
                    if (reFailsafeDays.search(dataLine) && reFailsafeDays.matches())
                        failsafe_days = stoi(reFailsafeDays.get_match(2));
                else    // notify
                    if (reNotify.search(dataLine) && reNotify.matches()) {
                        Pcre reDelimiters("\\s*[,;]\\s*", "g");
                        auto notifies = reDelimiters.split(reNotify.get_match(2));
                        for (auto not_it = notifies.begin(); not_it != notifies.end(); ++not_it)
                            notify.insert(*not_it);
                    }
                else    // skip blank lines
                    if (reBlank.search(dataLine))
                        continue;
                else {
                    configFile.close();
                    log("error: unknown configuration directive on line " + to_string(line) + " of " + filename);
                    cerr << "error: unknown configuration directive on line " << line << " of " << filename << endl;
                    cerr << "    " << dataLine << endl;
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

