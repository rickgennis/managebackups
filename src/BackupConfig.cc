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

// define CAPTURE_VALUE string("(\\s|=|:)+([^#]+)")
// define RE_COMMENT "(\\s*(#|//).*)"

class Config_Setting {
    public:
        Pcre regex;
        int var_type;   // string, int, set
        bool seen;
        void *variable;

        string getValue();
        void setValue(string);
        Config_Setting(string pattern, int newType, void *realData);
}; 


Config_Setting::Config_Setting(string pattern, int newType, void *realData) {
    regex = Pcre(pattern + CAPTURE_VALUE + RE_COMMENT);
    var_type = newType;
    seen = false;
    variable = realData;
}


string Config_Setting::getValue() {
    seen = true;

    switch (var_type) {
        case 0:  // string
            return *(string*)variable;

        case 1:  // int
            return to_string(*(unsigned int*)variable);

        case 2:  // set
        default:
            string result;
            set<string> theSet = *(set<string>*)variable;
            for (auto set_it = theSet.begin(); set_it != theSet.end(); ++set_it) {
                result += (result.length() ? ", " : "") + *set_it;
            }
            return result;
    }
}


void Config_Setting::setValue(string newValue) {
    switch (var_type) {
        case 0:  // string
            *(string*)variable = newValue;
            break;

        case 1:  // int
            *(unsigned int*)variable = stoi(newValue);
            break;

        case 2:  // set
        default:
            Pcre reDelimiters("\\s*[,;]\\s*", "g");
            auto values = reDelimiters.split(newValue);

            for (auto str_it = values.begin(); str_it != values.end(); ++str_it) 
                ((set<string>*)variable)->insert(*str_it);
    }
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
        settings.insert(settings.begin(), Config_Setting(RE_NOTIFY, 2, &notify));
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
                    if (cfg_it->regex.search(dataLine) && cfg_it->regex.matches() > 2) {
                        newFile << cfg_it->regex.get_match(0) << cfg_it->regex.get_match(1) << cfg_it->getValue() << 
                            (cfg_it->regex.matches() > 3 ? cfg_it->regex.get_match(3) : "")  << endl;
                        identified = true;
                        break;
                    }
                }

                // comment out unrecognized settings
                if (!identified && !reBlank.search(dataLine)) {
                    newFile << "// " << dataLine << "       # unknown setting?" << endl; 
                }
                else  // add as is (likely a comment)
                    if (!identified)
                        newFile << dataLine << endl;
            }

            oldFile.close();
            newFile.close();
            //remove(config_filename.c_str());
            //rename(temp_filename.c_str(), config_filename.c_str());
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
        settings.insert(settings.begin(), Config_Setting(RE_NOTIFY, 2, &notify));

        Pcre reBlank(RE_BLANK);
        config_filename = filename;

        unsigned int line = 0;
        try {
            while (getline(configFile, dataLine)) {
                ++line;

                // compare the line against each of the config settings until there's a match
                bool identified = false;
                for (auto cfg_it = settings.begin(); cfg_it != settings.end(); ++cfg_it) {
                    cerr << dataLine << endl;
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

