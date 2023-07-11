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

extern void cleanupAndExitOnError();

using namespace pcrepp;

bool operator<(const BackupConfig& b1, const BackupConfig& b2) {
    return (b1.settings[sTitle].value < b2.settings[sTitle].value);
}


bool operator>(const BackupConfig& b1, const BackupConfig& b2) {
    return (b1.settings[sTitle].value > b2.settings[sTitle].value);
}

bool operator==(const BackupConfig& b1, const BackupConfig& b2) {
    return (b1.settings[sTitle].value == b2.settings[sTitle].value);
}

BackupConfig::BackupConfig(bool makeTemp) {
    modified = 0;
    temp = makeTemp;
    config_filename = "";

    // define settings and their defaults
    // *** order *** of these inserts matter because they're accessed by position via the SetSpecifier enum
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
    settings.insert(settings.end(), Setting(CLI_MAXLINKS, RE_MAXLINKS, INT, "200"));
    settings.insert(settings.end(), Setting(CLI_TIME, RE_TIME, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_NOS, RE_NOS, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_MINSIZE, RE_MINSIZE, SIZE, "500"));
    settings.insert(settings.end(), Setting(CLI_DOW, RE_DOW, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_FS_FP, RE_FS_FP, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_MODE, RE_MODE, OCTAL, "0600"));
    settings.insert(settings.end(), Setting(CLI_MINSPACE, RE_MINSPACE, SIZE, "0"));
    settings.insert(settings.end(), Setting(CLI_MINSFTPSPACE, RE_MINSFTPSPACE, SIZE, "0"));
    settings.insert(settings.end(), Setting(CLI_NICE, RE_NICE, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_TRIPWIRE, RE_TRIPWIRE, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_NOTIFYEVERY, RE_NOTIFYEVERY, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_MAILFROM, RE_MAILFROM, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_LEAVEOUTPUT, RE_LEAVEOUTPUT, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_FAUB, RE_FAUB, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_UID, RE_UID, INT, "-1"));
    settings.insert(settings.end(), Setting(CLI_GID, RE_GID, INT, "-1"));
    settings.insert(settings.end(), Setting(CLI_CONSOLIDATE, RE_CONSOLIDATE, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_BLOAT, RE_BLOAT, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_UUID, RE_UUID, STRING, ""));
    settings.insert(settings.end(), Setting(CLI_FS_SLOW, RE_FSSLOW, INT, "0"));
    settings.insert(settings.end(), Setting(CLI_DEFAULT, RE_DEFAULT, BOOL, "false"));
    settings.insert(settings.end(), Setting(CLI_DATAONLY, RE_DATAONLY, BOOL, "false"));

    // CLI_PATHS is intentionally left out because its only accessed via CLI
    // and never as a Setting.  to implement it as a Setting would require a new
    // type (vector<string>) to be setup and parse and there's really no benefit.
}


BackupConfig::~BackupConfig() {
/*    if (modified) {
        saveConfig();
        modified = 0;
    }*/
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
        if (mystat((GLOBALS.confDir + baseName + ".conf"), &statBuf)) {
            int suffix = 1;
            while (mystat((GLOBALS.confDir + baseName + to_string(suffix) + ".conf"), &statBuf))
                ++suffix;

            config_filename = GLOBALS.confDir + baseName + to_string(suffix) + ".conf";
        }
    }

    NOTQUIET && cout << "\t• saving profile " << settings[sTitle].value << " (" << config_filename << ")" << endl;
    
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
        cleanupAndExitOnError();
        
    }

    string usersDelimiter = ": ";
    if (oldFile.is_open()) {
        Pcre reBlank(RE_BLANK);

        string dataLine;
        unsigned int line = 0;
        bool bFailsafeParanoid = str2bool(settings[sFP].value);

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
                                                        
                            // don't write fs_days, fs_backups or fs_slow if fp is set
                            if (!bFailsafeParanoid || (bFailsafeParanoid && (setting.display_name != CLI_FS_DAYS && setting.display_name != CLI_FS_BACKUPS && setting.display_name != CLI_FS_SLOW))) {
                                newFile << setting.regex.get_match(0) << setting.regex.get_match(1) <<
                                (setting.data_type == BOOL ? (str2bool(setting.value) ? "true" : "false") : setting.value) <<
                                (setting.regex.matches() > 3 ? setting.regex.get_match(3) : "")  << endl;
                                setting.seen = identified = true;
                                break;
                            }
                            else
                                if (setting.display_name == CLI_FS_DAYS || setting.display_name == CLI_FS_BACKUPS || setting.display_name == CLI_FS_SLOW)
                                    setting.seen = identified = true;
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
            cleanupAndExitOnError();
        }

        oldFile.close();

        // loop through settings that weren't specified in the existing config;
        // if any of the current values (likely specified via command line parameters on startup)
        // differ from the defaults, write them to the new file.
        for (auto &setting: settings)
            if (!setting.seen && (setting.value != setting.defaultValue)) {
                
                // for UUID add a comment
                if (setting.display_name == CLI_UUID)
                    newFile << setting.display_name << usersDelimiter << setting.value << "\t\t# links config to cache (do not change)"<< endl;
                else
                    // if fp is set then don't add fs_days or fs_backups
                    if (!str2bool(settings[sFP].value) || (setting.display_name != CLI_FS_DAYS && setting.display_name != CLI_FS_BACKUPS))
                        newFile << setting.display_name << usersDelimiter << (setting.data_type == BOOL ? (str2bool(setting.value) ? "true" : "false") : setting.value) << endl;
            }
    }
    else {
        // completely new config, first time being written
        string commentLine = "#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#-#\n";
        newFile << commentLine << "# Profile created on " << todayString() << "\n" << commentLine;
        newFile << blockp("profile:", -17) << settings[sTitle].value << "\n\n\n";
        newFile << commentLine << "# Backing up\n" << commentLine << "\n";

        newFile << settings[sDirectory].confPrint("/var/backups");
        newFile << settings[sBackupFilename].confPrint("myuser.tgz");
        newFile << settings[sBackupCommand].confPrint("tar czf - /usr/local/bin");
        newFile << settings[sFaub].confPrint("ssh remoteserver managebackups --path /usr/local/bin");
        newFile << settings[sNotify].confPrint("me@zmail.com, marvin@ymail.com");
        newFile << settings[sSFTPTo].confPrint("myremotebox:/backups");
        newFile << settings[sSCPTo].confPrint("myremotebox:/backups/{subdir}/{file}");
        newFile << settings[sMode].confPrint();
        newFile << settings[sIncTime].confPrint();
        newFile << settings[sMinSpace].confPrint("2G");
        newFile << settings[sBloat].confPrint() << "\n\n";

        newFile << commentLine << "# Pruning\n" << commentLine << "\n";
        newFile << settings[sPruneLive].confPrint();
        newFile << settings[sDays].confPrint();
        newFile << settings[sWeeks].confPrint();
        newFile << settings[sMonths].confPrint();
        newFile << settings[sYears].confPrint();
        newFile << settings[sFailsafeBackups].confPrint();
        newFile << settings[sFailsafeDays].confPrint();
        newFile << settings[sConsolidate].confPrint();
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
                
                // skip blanks and comments
                if (reBlank.search(dataLine))
                    continue;

                // compare the line against each of the config settings until there's a match
                bool identified = false;
                for (auto &setting: settings) {
                    if (setting.regex.search(dataLine) && setting.regex.matches() > 2) {
                        setting.value = setting.regex.get_match(2);
                        // STRING is handled implicitly with no conversion
                        
                        // special-case for something that can look like a SIZE or be a percentage
                        if (setting.display_name == CLI_BLOAT) {
                            for (int i=0; i < setting.value.length(); ++i)
                                if (!isdigit(setting.value[i]) && setting.value[i] != '%') {
                                    try {
                                        approx2bytes(setting.value);
                                    }
                                    catch (...) {
                                        log("error: unable to parse value for the directive on line " + to_string(line) + " of " + filename);
                                        SCREENERR("error: unable to parse value on line " << line << " of " << filename << "\n" <<
                                                  "it should be a size (e.g. 25G) or a percentage (e.g. 75%)\n");
                                        exit(1);
                                    }
                                    
                                    break;
                                }
                        }
                        
                        if (setting.data_type == INT)
                            stoi(setting.value);    // will throw on invalid value
                        else 
                            if (setting.data_type == OCTAL)
                                try {
                                    // validate mode -- will throw on invalid value
                                    stol(setting.value, NULL, 8);
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
                                    approx2bytes(setting.value);
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

                if (!identified) {
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
        
        // if there's no UUID for this profile create one
        if (!settings[sUUID].value.length()) {
            char hostname[1024];
            if (gethostname(hostname, sizeof(hostname)))
                hostname[0] = 0;
            
            settings[sUUID].value = MD5string(to_string(time(NULL)) + to_string(getpid()) + string(hostname) + to_string(getuid()) + settings[sTitle].value);
            modified = true;
        }
        
        DEBUG(D_config) DFMT("successfully parsed [" << settings[sTitle].value << "] config from " << filename);

        return 1;
    }
    else {
        SCREENERR(log("error: unable to read " + filename + errtext()));
    }

    return 0;
}


void BackupConfig::loadConfigsCache() {
    if (settings[sDirectory].value.length() && settings[sBackupFilename].value.length()) {
        cache.setUUID(settings[sUUID].value);

        if (!cache.restoreCache())
            mkdirp(GLOBALS.cacheDir);
    }
}


string BackupConfig::ifTitle() {
    return (settings[sTitle].value.length() ? string("[") + settings[sTitle].value + "]" : "");
}

void BackupConfig::fullDump() {
    for (auto &setting: settings)
        cout << "setting " << setting.display_name << ": " << setting.value << endl;
}


// this is fugly and should be refactored
unsigned int BackupConfig::removeEmptyDirs(string directory, int baseSlashes) {
    DIR *dir;
    struct dirent *dirEntry;
    vector<string> subDirs;
    string startDir = directory.length() ? directory : settings[sDirectory].value;

    int numBaseSlashes = baseSlashes ? baseSlashes : (int)count(startDir.begin(), startDir.end(), '/');

    /* this is unique logic (needs depth-first) so processDirectory() won't work; let's traverse the directories here instead */
    if ((dir = opendir(ue(startDir).c_str())) != NULL) {
        unsigned int entryCount = 0;

        while ((dirEntry = readdir(dir)) != NULL) {

            if (!strcmp(dirEntry->d_name, ".") || !strcmp(dirEntry->d_name, ".."))
               continue; 

            ++entryCount;
            struct stat statData;
            string fullFilename = slashConcat(startDir, dirEntry->d_name);

            auto depth = count(fullFilename.begin(), fullFilename.end(), '/') - numBaseSlashes;
            bool entIsDay = (string(dirEntry->d_name).length() == 2 && isdigit(dirEntry->d_name[0]) && isdigit(dirEntry->d_name[1]));

            if ((depth < 3 || (depth == 3 && entIsDay)) && !mystat(fullFilename, &statData)) {
                if (S_ISDIR(statData.st_mode)) {
                    subDirs.insert(subDirs.end(), fullFilename);
                }
            }
        }
        closedir(dir);
        
        for (auto &dir: subDirs) {
            if (!removeEmptyDirs(dir, numBaseSlashes)) {
                
                if (!rmdir(dir.c_str())) {    // remove empty subdirectory
                    NOTQUIET && cout << "\t• removed empty directory " << dir << endl;
                    log(ifTitle() + " removed empty directory " + dir);
                    --entryCount;
                }
                else {
                    SCREENERR("error: unable to remove empty directory " << dir);
                    log(ifTitle() + " error: unable to remove empty directory " + dir);
                }
            }
        }
        
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
    string stateFilename = slashConcat(GLOBALS.cacheDir, settings[sUUID].value, settings[sUUID].value) + ".state";
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
    string stateFilename = slashConcat(GLOBALS.cacheDir, settings[sUUID].value, settings[sUUID].value) + ".state";
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

/* calculate the average size of the most rercent 'maxBackups'
   backups. because backups can be large & the last several of
   them added together may be huge, we check for variable
   overflow. if it overflows we may end up using fewer than
   'maxBackups' backups.
 */
size_t BackupConfig::getRecentAvgSize(int maxBackups) {
    auto cacheSize = cache.indexByFilename.size();
    auto fcacheSize = fcache.size();
    size_t runningTotal = 0;
    int counted = 0;
    
    if (cacheSize > fcacheSize) {
        auto backupIt = cache.indexByFilename.end();
        
        if (cacheSize)
            while (--backupIt != cache.indexByFilename.begin() && counted < maxBackups) {
                auto rawIt = cache.rawData.find(backupIt->second);
                if (rawIt != cache.rawData.end()) {
                    
                    // check for overflow
                    if (runningTotal > SIZE_MAX - rawIt->second.size)
                        break;
                    
                    ++counted;
                    runningTotal += rawIt->second.size;
                }
            }
    }
    else {
        auto backupIt = fcache.getEnd();
        
        if (fcacheSize)
            while (--backupIt != fcache.getFirstBackup() && counted < maxBackups) {
                
                // check for overflow
                if (runningTotal > SIZE_MAX - backupIt->second.ds.sizeInBytes)
                    break;
                
                ++counted;
                runningTotal += backupIt->second.ds.sizeInBytes;
            }
    }

    if (counted < maxBackups)
        log("warning: used " + to_string(counted) + " instead of " + to_string(maxBackups) + " backups for average due to variable overflow");
    
    DEBUG(D_backup) DFMT("examined " << counted << " out of " << maxBackups << ", average is " << (counted ? runningTotal / counted : 0));
    return (counted ? runningTotal / counted : 0);
}


size_t BackupConfig::getBloatTarget() {
    string bloat = settings[sBloat].value;
    size_t target = getRecentAvgSize();
    size_t origTarget = target;
    
    if (bloat.find("%") == string::npos) {
        auto bytes = approx2bytes(bloat);
        target += bytes;
        DEBUG(D_backup) DFMT("avg " << origTarget << " + " << bloat << " (" << bytes << ") = " << target);
    }
    else {
        bloat.erase(bloat.find("%"), string::npos);  // remove % sign
        int percent = stoi(bloat);
        target = percent / 100.0 * target;
        DEBUG(D_backup) DFMT("avg " << origTarget << " * " << settings[sBloat].value << " = " << target);
    }
    
    return target;
}

void BackupConfig::renameBaseDirTo(string newBaseDir) {
    settings[sDirectory].value = newBaseDir;
    saveConfig();
}
