
#ifndef SETUP_C
#define SETUP_C

#include <fstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

#include "globals.h"
#include "util_generic.h"
#include "help.h"


using namespace std;

void installman() {
    string manPath = "/usr/local/share/man/man1";

    struct stat statBuf;
    if (stat(manPath.c_str(), &statBuf)) {
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
        string destbin = "/usr/local/bin/managebackups";
        string cp = locateBinary("cp");
        mode_t setgidmode = S_ISGID | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
        mode_t setuidmode = S_ISUID | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;

        // mkdir -p GLOBALS.confDir
        if (stat(GLOBALS.confDir.c_str(), &statBuf)) {
            if (mkdirp(GLOBALS.confDir)) {
                SCREENERR("error: unable to create " << GLOBALS.confDir);                
                goto bailout;
            }
            cout << "mkdir -p " << GLOBALS.confDir << " (success)\n";
        }

        // mkdir -p GLOBALS.cacheDir
        if (stat(GLOBALS.cacheDir.c_str(), &statBuf)) {
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

        if (stat("/usr/local/bin", &statBuf)) {
            if (mkdirp("/usr/local/bin")) {
                SCREENERR("error: unable to create /usr/local/bin");
                goto bailout;
            }
            cout << "created /usr/local/bin (success)\n";
        }

        if (cp.length()) {
            system(string(cp + " " + myBinary + " " + destbin).c_str());
            cout << "installed " << destbin << "\n";
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

#endif

