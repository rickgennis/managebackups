
#ifndef SETUP_C
#define SETUP_C

#include <fstream>
#include <string>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <time.h>

#include "globals.h"
#include "util_generic.h"
#include "help.h"
#include "ipc.h"
#include <pcre++.h>


using namespace pcrepp;
using namespace std;


void installman() {
    string manPath = "/usr/local/share/man/man1";

    struct stat statBuf;
    if (mystat(manPath, &statBuf)) {
        if (mkdirp(manPath)) {
            SCREENERR("error: unable to create " << manPath);
            return;
        }
        cout << "mkdir -p " << manPath << " (success)\n";
    }

    ofstream manf;
    manf.open(manPath + "/managebackups.1");

    // create the man page
    if (manf.is_open()) {
        manf << manPathContent();
        manf.close();

        cout << "created " << manPath << "/managebackups.1 (success)\n";

        // compress the man page
        string gzip = locateBinary("gzip");
        if (gzip.length()) {
            unlink(string(manPath + "/managebackups.1.gz").c_str());
            system(string(gzip + " " + manPath + "/managebackups.1").c_str());
            cout << "compressed to " << manPath << "/managebackups.1.gz (success)\n";
        }
    }
}


void install(string myBinary, bool suid) {
    ifstream groupf;
    int gid = 0;

    groupf.open("/etc/group");
    if (groupf.is_open()) {
        string line;

        try {
            while (getline(groupf, line)) {
                // find the "daemon" line
                if (line.find("daemon") != string::npos) {
                    vector<string> parts;
                    stringstream tokenizer(line);
                    string tempStr;

                    // break the line into fields
                    while (getline(tokenizer, tempStr, ':'))
                        parts.push_back(tempStr);

                    gid = stoi(parts[2]);
                    break;
                }
            }
        }
        catch (...) {
            gid = 0;
        }

        groupf.close();
    }

    if (gid) {
        struct stat statBuf;
        string destbindir = "/usr/local/bin/";
        string destbin = destbindir + "managebackups";
        string cp = locateBinary("cp");
        mode_t setgidmode = S_ISGID | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
        mode_t setuidmode = S_ISUID | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;

        // mkdir -p GLOBALS.confDir
        if (mystat(GLOBALS.confDir, &statBuf)) {
            if (mkdirp(GLOBALS.confDir)) {
                SCREENERR("error: unable to create " << GLOBALS.confDir);                
                goto bailout;
            }
            cout << "mkdir -p " << GLOBALS.confDir << " (success)\n";
        }

        // mkdir -p GLOBALS.cacheDir
        if (mystat(GLOBALS.cacheDir, &statBuf)) {
            if (mkdirp(GLOBALS.cacheDir)) {
                SCREENERR("error: unable to create " << GLOBALS.cacheDir);
                goto bailout;
            }
            cout << "mkdir -p " << GLOBALS.cacheDir << " (success)\n";
        }

        // chgrp gid GLOBALS.confDir
        if (chown(GLOBALS.confDir.c_str(), -1, gid)) {
            SCREENERR("error: unable to chgrp of " << GLOBALS.confDir << " to " << to_string(gid));
            goto bailout;
        }
        cout << "chgrp " << to_string(gid) << " " << GLOBALS.confDir << " (success)\n";

        // chgrp gid GLOBALS.cacheDir
        if (chown(GLOBALS.cacheDir.c_str(), -1, gid)) {
            SCREENERR("error: unable to chgrp of " << GLOBALS.cacheDir << " to " << to_string(gid));
            goto bailout;
        }
        cout << "chgrp " << to_string(gid) << " " << GLOBALS.cacheDir << " (success)\n";

        // setgid GLOBALS.confDir
        if (chmod(GLOBALS.confDir.c_str(), setgidmode | S_IROTH | S_IXOTH)) {
            SCREENERR("error: unable to setgid (chmod) to " << GLOBALS.confDir);
            goto bailout;
        }
        cout << "chmod 2775 " << GLOBALS.confDir << " (success)\n";

        // setgid GLOBALS.cacheDir
        if (chmod(GLOBALS.cacheDir.c_str(), setgidmode)) {
            SCREENERR("error: unable to setgid (chmod) to " << GLOBALS.cacheDir);
            goto bailout;
        }
        cout << "chmod 2770 " << GLOBALS.cacheDir << " (success)\n";

        if (mystat("/usr/local/bin", &statBuf)) {
            if (mkdirp("/usr/local/bin")) {
                SCREENERR("error: unable to create /usr/local/bin");
                goto bailout;
            }
            cout << "created /usr/local/bin (success)\n";
        }

        if (cp.length()) {
            system(string(cp + " " + myBinary + " " + destbin).c_str());
            cout << "installed " << destbin << "\n";
            
            if (!exists(destbindir + "mb")) {
                if (!symlink(string(destbindir + "managebackups").c_str(), string(destbindir + "mb").c_str()))
                    cout << "mb symlink created\n";
                else
                    cout << RED << "unable to create " << destbindir << "mb symlink: " << errtext() << RESET << endl;
            }
        }

        if (suid) {
            if (chmod(destbin.c_str(), setuidmode | S_IROTH | S_IXOTH)) {
                SCREENERR("error: unable to chmod 4755 " << destbin);
                goto bailout;
            }
            cout << "chmod 4755 " << destbin << " (success)\n";
        }
        else {
            if (chown(destbin.c_str(), -1, gid)) {
                SCREENERR("error: unable to chgrp of " << destbin << " to " << to_string(gid));
                goto bailout;
            }
            cout << "chgrp " << to_string(gid) << " " << destbin << " (success)\n";

            if (chmod(destbin.c_str(), setgidmode | S_IROTH | S_IXOTH)) {
                SCREENERR("error: unable to chmod 2755 " << destbin);
                goto bailout;
            }
            cout << "chmod 2755 " << destbin << " (success)\n";
        }

        {
            string logfile = "/var/log/managebackups.log";
            ofstream logF;
            logF.open(logfile, ios::app);

            if (logF.is_open()) {
                logF.close();

                cout << "touch " << logfile << " (success)\n";

                if (chown(logfile.c_str(), -1, gid)) {
                    SCREENERR("error: unable to chgrp of " << logfile << " to " << to_string(gid));
                    goto bailout;
                }
                cout << "chgrp " << to_string(gid) << " " << logfile << " (success)\n";

                if (chmod(logfile.c_str(), setgidmode)) {
                    SCREENERR("error: unable to chmod 2770 " << logfile);
                    goto bailout;
                }
                cout << "chmod 2770 " << logfile << " (success)\n";
            }
        }
    
        installman();        

        cout << BOLDRED << "Use 'man managebackups' for full detail." << RESET << "\n";
        cout << BOLDBLUE << "Installation complete." << RESET << endl;
        return;
        
        bailout:
        cerr << "Installation halted." << endl;
    }
    else {
        SCREENERR("error: unable to locate the daemon group in /etc/group. You can select a different group to use\n" <<
            "from the group file. Use its name in place of GROUP in these commands:\n" <<
            "\tmkdir -p " << GLOBALS.confDir << "\n" <<
            "\tmkdir -p " << GLOBALS.cacheDir << "\n" <<
            "\tchgrp GROUP " << GLOBALS.confDir << "\n" <<
            "\tchgrp -R GROUP " << GLOBALS.cacheDir << "\n" <<
            "\tchmod -R 2775 " << GLOBALS.confDir << "\n" <<
            "\tchmod -R 2770 " << GLOBALS.cacheDir);
    }

    return;
}


string getPlistHeader(string& appPath) {
    string text = R"EOF(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
    <dict>
        <key>Label</key>
            <string>com.local.managebackups</string>
        <key>Program</key>
                <string>APPPATH</string>
        <key>ProgramArguments</key>
            <array>
                <string>APPPATH</string>
                <string>-K</string>
            </array>
        <key>ProcessType</key>
            <string>Background</string>
        <key>StartCalendarInterval</key>
            <array>
)EOF";

    // replace both occurances of apppath
    text.replace(text.find("APPPATH"), 7, appPath);
    text.replace(text.find("APPPATH"), 7, appPath);

    return text;
}


string getPlistTimeBlock(int hour, int minute) {
    string text = R"EOF(            <dict>
                <key>Hour</key>
                <integer>HOUR></integer>
                <key>Minute</key>
                <integer>MINUTE</integer>
            </dict>
)EOF";
    
    // replace hour and minute
    text.replace(text.find("HOUR"), 4, to_string(hour));
    text.replace(text.find("MINUTE"), 6, to_string(minute));
    return text;
}


void scheduleLaunchCtl(string& appPath) {
    string userHomeDir = getUserHomeDir();
    string plistPath = slashConcat(userHomeDir, "Library/LaunchAgents/com.local.managebackups.plist");

    // create the plist file
    // *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
    ofstream plistFile;
    plistFile.open(plistPath);
    
    if (plistFile.is_open()) {
        
        // write header portion
        plistFile << getPlistHeader(appPath);
        
        int offset = GLOBALS.cli[CLI_SCHED].as<int>();
        int minute = GLOBALS.cli.count(CLI_SCHEDMIN) ? GLOBALS.cli[CLI_SCHEDMIN].as<int>() : 15;

        if (offset == 0 || offset == 24) {
            int hour = GLOBALS.cli.count(CLI_SCHEDHOUR) ? GLOBALS.cli[CLI_SCHEDHOUR].as<int>() : 0;
            plistFile << getPlistTimeBlock(hour, minute);
        }
        else {
            int hour = 0;
            while (hour < 24) {
                plistFile << getPlistTimeBlock(hour, minute);
                hour += offset;
            }
        }
        
        plistFile << R"EOF(        </array>
    </dict>
</plist>
)EOF";
        
        plistFile.close();

        // execute launchctl to activate the file
        // *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
        auto uid = getuid();
        string plistFn = slashConcat(userHomeDir, "Library/LaunchAgents/com.local.managebackups.plist");
        string devnull = " 2> /dev/null";
        
        string command = "/bin/launchctl bootout gui/UID/com.local.managebackups 2> /dev/null";
        command.replace(command.find("UID"), 3, to_string(uid));
        system(command.c_str());
        
        command = "/bin/launchctl bootstrap gui/UID " + plistFn;
        command.replace(command.find("UID"), 3, to_string(uid));
        if (!system(string(command + devnull).c_str())) {
            
            command = "/bin/launchctl enable gui/UID/com.local.managebackups";
            command.replace(command.find("UID"), 3, to_string(uid));
            if (!system(string(command + devnull).c_str())) {

                cout << "successfully created property file (" << plistFn << ").\n";
                cout << "successfully scheduled to run ";
                if (offset == 0 || offset == 24)
                    cout << "daily." << endl;
                else
                    cout << "every " << plural(offset, "hour") << ".\n";
                cout << "use \"launchctl disable gui/" << uid << "/com.local.managebackups\" to disable." << endl;
            }
            else {
                SCREENERR("error enabling service. Try \"" << command << "\"")
                exit(1);
            }
        }
        else {
            SCREENERR("error loading service. Try \"" << command << "\"");
            exit(1);
        }
    }
    else {
        SCREENERR("error: unable to write to " << plistPath)
        exit(1);
    }
}


string newCronTimes() {
    int offset = GLOBALS.cli[CLI_SCHED].as<int>();
    int minute = GLOBALS.cli.count(CLI_SCHEDMIN) ? GLOBALS.cli[CLI_SCHEDMIN].as<int>() : 15;

    if (offset == 0 || offset == 24) {
        int hour = GLOBALS.cli.count(CLI_SCHEDHOUR) ? GLOBALS.cli[CLI_SCHEDHOUR].as<int>() : 0;
        return(to_string(minute) + " " + to_string(hour) + " * * *");
    }
    
    return (to_string(minute) + " */" + to_string(offset) + " * * *");
}


void scheduleCron(string& appPath) {
    string crontabCmd = exists("/usr/bin/crontab") ? "/usr/bin/crontab" : "crontab";
    
    PipeExec cronOutput(crontabCmd + " -l");
    cronOutput.execute();
    
    int bytesRead;
    char data[1024 * 64];
    string oldUserTab;
    
    while ((bytesRead = (int)cronOutput.ipcRead(data, sizeof(data))) > 0)
        oldUserTab += string(data, bytesRead);
    
    cronOutput.closeAll();
    
    Pcre commentRE("^\\s*#");
    Pcre lineRE("\\s*\\S+\\s+\\S+\\s+\\S+\\s+\\S+\\s+\\S+\\s+(.+)");
    string line, newUserTab;
    size_t index;
    istringstream iss(oldUserTab);

    bool found = false;
    while (getline(iss, line)) {
        if (((index = line.find("managebackups")) != string::npos) && !commentRE.search(line)) {
            if (lineRE.search(line) && lineRE.matches()) {
                line = newCronTimes() + "  " + lineRE.get_match(0);
                found = true;
            }
        }
        
        newUserTab += line + "\n";
    }

    if (!found) {
        newUserTab += "\n# managebackups cron'd on " + string(asctime(localtime(&GLOBALS.startupTime)));
        newUserTab += newCronTimes() + "  " + appPath + " -K\n";
    }
    
    PipeExec cronEdit(crontabCmd + " -");
    cronEdit.execute();
    
    istringstream iss2(newUserTab);
    
    bool success = true;
    while (getline(iss2, line)) {
        line += "\n";
        if (cronEdit.ipcWrite(line.c_str(), line.length()) < line.length())
            success = false;
    }
    
    cronEdit.closeAll();
    
    if (success) {
        int offset = GLOBALS.cli[CLI_SCHED].as<int>();
        cout << "successfully scheduled cron job for every " << plural(offset, "hour") << "." << endl;
        cout << "use 'crontab -e' to make changes." << endl;
    }
    else
        cout << "error attempting to update crontab; use 'crontab -e' to edit manually.";
}


void scheduleRun() {
    string appName = "managebackups";
    
    // make sure we're working with a full path to the app not just its dir
    string appPath = GLOBALS.cli.count(CLI_SCHEDPATH) ? GLOBALS.cli[CLI_SCHEDPATH].as<string>() : ("/usr/local/bin/" + appName);
    if (!(appPath.length() >= appName.length() && appPath.substr(appPath.length() - appName.length()) == appName))
        appPath = slashConcat(appPath, appName);

    // verify --sched is a valid number of hours
    int offset = GLOBALS.cli[CLI_SCHED].as<int>();
    if (offset != 0 && 24 % offset) {
        SCREENERR("error: --sched specifies the number of hours between runs; it needs to be\n" <<
                  "a number that divides evenly into 24 (i.e. 0, 1, 2, 3, 4, 6, 8, 12, 24)");
        exit(1);
    }
    
    if ((offset != 0 && offset != 24) && GLOBALS.cli.count(CLI_SCHEDHOUR)) {
        SCREENERR("error: --schedhour is only valid with --sched set to 0 or 24; meaning you can\n"
                  << "only specify the hour to run when setting to run only once a day.");
        exit(1);
    }

#ifdef __APPLE__
    scheduleLaunchCtl(appPath);
#elif __linux__
    scheduleCron(appPath);
#endif
    
}

#endif

