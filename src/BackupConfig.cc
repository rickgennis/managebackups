#include <fstream>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <pcre++.h>

#include "BackupConfig.h"
#include "Setting.h"
#include "util_generic.h"
#include "globals.h"
#include "colors.h"
#include "debug.h"


using namespace pcrepp;

bool operator<(const BackupConfig& b1, const BackupConfig& b2) {
    return(b1.settings[sTitle].value < b2.settings[sTitle].value);
}


bool operator>(const BackupConfig& b1, const BackupConfig& b2) {
    return(b1.settings[sTitle].value > b2.settings[sTitle].value);
}


BackupConfig::BackupConfig(bool makeTemp) {
    modified = 0;
    temp = makeTemp;
    config_filename = "";

    // define settings and their defaults
    // order of these inserts matter because they're accessed by position via the SetSpecifier enum
    settings.insert(settings.end(), Setting(CLI_PROFILE, RE_PROFILE, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_DIR, RE_DIR, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_FILE, RE_FILE, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_COMMAND, RE_CMD, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_DAYS, RE_DAYS, INT, "14"));
    settings.insert(settings.end(), Setting(CLI_WEEKS, RE_WEEKS, INT, "4"));
    settings.insert(settings.end(), Setting(CLI_MONTHS, RE_MONTHS, INT, "6"));
    settings.insert(settings.end(), Setting(CLI_YEARS, RE_YEARS, INT, "2"));
    settings.insert(settings.end(), Setting(CLI_FS_BACKUPS, RE_FSBACKUPS, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_FS_DAYS, RE_FSDAYS, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_SCPTO, RE_SCP, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_SFTPTO, RE_SFTP, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_PRUNE, RE_PRUNE, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_NOTIFY, RE_NOTIFY, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_MAXLINKS, RE_MAXLINKS, INT, "100"));
    settings.insert(settings.end(), Setting(CLI_TIME, RE_TIME, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_NOS, RE_NOS, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_MINSIZE, RE_MINSIZE, SIZE, "500"));
    settings.insert(settings.end(), Setting(CLI_DOW, RE_DOW, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_FS_FP, RE_FS_FP, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_MODE, RE_MODE, OCTAL, "0600"));
    settings.insert(settings.end(), Setting(CLI_MINSPACE, RE_MINSPACE, SIZE, "0"));
    settings.insert(settings.end(), Setting(CLI_MINSFTPSPACE, RE_MINSFTPSPACE, SIZE, "0"));
    settings.insert(settings.end(), Setting(CLI_NICE, RE_NICE, INT, "10"));
    settings.insert(settings.end(), Setting(CLI_TRIPWIRE, RE_TRIPWIRE, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_NOTIFYEVERY, RE_NOTIFYEVERY, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_MAILFROM, RE_MAILFROM, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_LEAVEOUTPUT, RE_LEAVEOUTPUT, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_FAUB, RE_FAUB, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_UID, RE_UID, INT, "-1"));
    settings.insert(settings.end(), Setting(CLI_GID, RE_GID, INT, "-1"));
    // CLI_PATHS is intentionally left out because its only accessed via CLI
    // and never as a Setting.  to implement it as a Setting would require a new
    // type (vector<string>) to be setup and parse and there's really no benefit.
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
        if (stat((GLOBALS.confDir + baseName + ".conf").c_str(), &statBuf)) {
            int suffix = 1;
            while (stat((GLOBALS.confDir + baseName + to_string(suffix) + ".conf").c_str(), &statBuf)) 
                ++suffix;

            config_filename = GLOBALS.confDir + baseName + to_string(suffix) + ".conf";
        }
    }

    if (GLOBALS.cli.count(CLI_RECREATE))
        unlink(config_filename.c_str());

    // open existing and new config files
    string temp_filename = config_filename + ".tmp." + to_string(GLOBALS.pid);
    oldFile.open(config_filename);
    newFile.open(temp_filename);

    if (!newFile.is_open()) {
        cerr << "error: unable to create " << temp_filename << " (directory not writable?)" << endl << endl;
        cerr << "When --save is specified managebackups writes its configs to " << GLOBALS.confDir << "." << endl;
        cerr << "An initial --save run via sudo is sufficient then leave --save off of subsequent runs." << endl;
        cerr << "However, managebackups will always need write access to " << GLOBALS.cacheDir << "." << endl;
        cerr << "These locations can be overriden via --confdir, --cachedir or the environment variables\n";
        cerr << "MB_CONFDIR, MB_CACHEDIR." << endl;
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
                    for (auto &setting: settings) {
                        if (setting.regex.search(dataLine) && setting.regex.matches() > 2) {
                            usersDelimiter = setting.regex.get_match(1);
                            newFile << setting.regex.get_match(0) << setting.regex.get_match(1) << 
                                (setting.data_type == BOOL ? (str2bool(setting.value) ? "true" : "false") : setting.value) << 
                                (setting.regex.matches() > 3 ? setting.regex.get_match(3) : "")  << endl;
                            setting.seen = identified = true;
                            break;
                        }
                    }

                    // comment out unrecognized settings
                    if (!identified) { 
                        newFile << "# " << dataLine << "       # unknown setting?" << endl; 
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

        // loop through settings that weren't specified in the existing config;
        // if any of the current values (likely specified via command line parameters on startup)
        // differ from the defaults, write them to the new file.
        for (auto &setting: settings)
            if (!setting.seen && (setting.value != setting.defaultValue)) {
                newFile << setting.display_name << usersDelimiter << setting.value << endl;
            }
    }
    else {
        // completely new config, first time being written
        string commentLine = "#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#\n";
        newFile << commentLine << "# Profile created on " << todayString() << "\n" << commentLine;
        newFile << blockp("profile:", -17) << settings[sTitle].value << "\n\n\n";
        newFile << commentLine << "# Backing up\n" << commentLine << "\n";

        newFile << settings[sDirectory].confPrint("/home/myuser");
        newFile << settings[sBackupFilename].confPrint("myuser.tgz");
        newFile << settings[sBackupCommand].confPrint("myuser.tgz");
        newFile << settings[sNotify].confPrint("me@zmail.com, marvin@ymail.com");
        newFile << settings[sSFTPTo].confPrint("myremotebox:/backups");
        newFile << settings[sSCPTo].confPrint("myremotebox:/backups/{subdir}/{file}");
        newFile << settings[sMode].confPrint();
        newFile << settings[sIncTime].confPrint();
        newFile << settings[sMinSpace].confPrint("2G") << "\n\n";

        newFile << commentLine << "# Pruning\n" << commentLine << "\n";
        newFile << settings[sPruneLive].confPrint();
        newFile << settings[sDays].confPrint();
        newFile << settings[sWeeks].confPrint();
        newFile << settings[sMonths].confPrint();
        newFile << settings[sYears].confPrint();
        newFile << settings[sFailsafeBackups].confPrint();
        newFile << settings[sFailsafeDays].confPrint();
        newFile << settings[sFP].confPrint() << "\n\n";

        newFile << commentLine << "# Linking\n" << commentLine << "\n";
        newFile << settings[sMaxLinks].confPrint();
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
                for (auto &setting: settings) {
                    if (setting.regex.search(dataLine) && setting.regex.matches() > 2) {
                        setting.value = setting.regex.get_match(2);

                        if (setting.data_type == INT)
                            auto ignored = stoi(setting.value);    // will throw on invalid value
                        else 
                            if (setting.data_type == OCTAL)
                                try {
                                    // validate mode -- will throw on invalid value
                                    int ignored = stol(setting.value, NULL, 8);
                                }
                                catch (...) {
                                    configFile.close();
                                    log("error: unable to parse an octal value for the directive on line " + to_string(line) + " of " + filename);
                                    cerr << "error: unable to parse an octal value for the directive on line " << line << " of " << filename << endl;
                                    cerr << "    " << dataLine << endl;
                                    exit(1);
                                }
                        else
                            if (setting.data_type == SIZE)
                                try {
                                    // validate size -- will throw on invalid value
                                    auto ignored = approx2bytes(setting.value);
                                }
                                catch (...) {
                                    configFile.close();
                                    log("error: unable to parse a valid size (e.g. 25K) for the directive on line " + to_string(line) + " of " + filename);
                                    cerr << "error: unable to parse a valid size for the directive on line " << line << " of " << filename << endl;
                                    cerr << "    " << dataLine << endl;
                                    exit(1);
                                }

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
        DEBUG(D_config) DFMT("successfully parsed [" << settings[sTitle].value << "] config from " << filename);

        return 1;
    }    

    return 0;
}


void BackupConfig::loadConfigsCache() {
    if (settings[sDirectory].value.length() && settings[sBackupFilename].value.length()) {
        cache.cacheFilename = GLOBALS.cacheDir + "/" + MD5string(settings[sDirectory].value + settings[sBackupFilename].value);
        struct stat statBuf;

        if (!stat(cache.cacheFilename.c_str(), &statBuf)) {
            cache.restoreCache();
        }
        else {
            mkdirp(GLOBALS.cacheDir);
        }
    }
}


string BackupConfig::ifTitle() {
    return (settings[sTitle].value.length() ? string("[") + settings[sTitle].value + "]" : "");
}

void BackupConfig::fullDump() {
    for (auto &setting: settings)
        cout << "setting " << setting.display_name << ": " << setting.value << endl;
}


unsigned int BackupConfig::removeEmptyDirs(string directory, int baseSlashes) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    string dir = directory.length() ? directory : settings[sDirectory].value;

    auto numBaseSlashes = baseSlashes ? baseSlashes : count(dir.begin(), dir.end(), '/');

    if ((c_dir = opendir(ue(dir).c_str())) != NULL) {
        unsigned int entryCount = 0;

        while ((c_dirEntry = readdir(c_dir)) != NULL) {

            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
               continue; 

            ++entryCount;
            ++GLOBALS.statsCount;
            struct stat statData;
            string fullFilename = slashConcat(dir, c_dirEntry->d_name);

            auto depth = count(fullFilename.begin(), fullFilename.end(), '/') - numBaseSlashes;
            bool entIsDay = (string(c_dirEntry->d_name).length() == 2 && isdigit(c_dirEntry->d_name[0]) && isdigit(c_dirEntry->d_name[1]));

            if ((depth < 3 || (depth == 3 && entIsDay)) && !stat(fullFilename.c_str(), &statData)) {
                if ((statData.st_mode & S_IFMT) == S_IFDIR) {

                    // recurse into subdirectories
                    if (!removeEmptyDirs(fullFilename, numBaseSlashes)) {

                        if (!rmdir(fullFilename.c_str())) {    // remove empty subdirectory
                            NOTQUIET && cout << ifTitle() << " removing empty directory " << fullFilename << endl;
                            log(ifTitle() + " removing empty directory " + fullFilename);
                            --entryCount;
                        }
                        else {
                            SCREENERR("error: unable to remove empty directory " << fullFilename);
                            log(ifTitle() + " error: unable to remove empty directory " + fullFilename);
                        }
                    }
                }
            }
        }

        closedir(c_dir);
        return entryCount;
    }

    return 1;
}


tuple<int, time_t> BackupConfig::getLockPID() {
    string lockFilename = GLOBALS.cacheDir + "/" + MD5string(settings[sDirectory].value + settings[sBackupFilename].value + settings[sTitle].value) + ".lock";
    unsigned int pid = 0;

    ifstream lockFile;
    lockFile.open(lockFilename);

    if (lockFile.is_open()) {
        string temp;
        lockFile >> temp;
        pid = stoi(temp);
        lockFile >> temp;
        time_t startTime;
        startTime = stol(temp);
        lockFile.close();

        return {pid, startTime};
    }

    return {0, 0};
}


string BackupConfig::setLockPID(unsigned int pid) {
    string lockFilename = GLOBALS.cacheDir + "/" + MD5string(settings[sDirectory].value + settings[sBackupFilename].value + settings[sTitle].value) + ".lock";
    mkdirp(GLOBALS.cacheDir);

    if (pid) {
        ofstream lockFile;
        lockFile.open(lockFilename);

        if (lockFile.is_open()) {
            lockFile << to_string(pid) << endl;
            lockFile << to_string(GLOBALS.startupTime) << endl;
            lockFile.close();
            return lockFilename;
        }

        log("unable to save lock to " + lockFilename + " (directory not writable?)");
        SCREENERR("error: unable to create " << lockFilename);
    }

    unlink(lockFilename.c_str());
    return "";
}


unsigned int BackupConfig::getPreviousFailures() {
    string stateFilename = GLOBALS.cacheDir + "/" + MD5string(settings[sDirectory].value + settings[sBackupFilename].value) + ".state";
    unsigned int count = 0;

    ifstream stateFile;
    stateFile.open(stateFilename);

    if (stateFile.is_open()) {
        string temp;
        stateFile >> temp;
        count = stoi(temp);
        stateFile.close();
    }

    return count;
}


void BackupConfig::setPreviousFailures(unsigned int count) {
    string stateFilename = GLOBALS.cacheDir + "/" + MD5string(settings[sDirectory].value + settings[sBackupFilename].value) + ".state";
    mkdirp(GLOBALS.cacheDir);

    ofstream stateFile;
    stateFile.open(stateFilename);

    if (stateFile.is_open()) {
        stateFile << to_string(count) << endl;
        stateFile.close();
    }
    else {
        log("unable to save state for notifications to " + stateFilename + " (directory not writable?)");
        SCREENERR("error: unable to create " << stateFilename);
    }
}

