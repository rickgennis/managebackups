
#ifndef BACKUPCONFIG_H
#define BACKUPCONFIG_H

#include <set>
#include <string>
using namespace std;

class BackupConfig {
        string config_filename;

    public:
        bool modified;

        string title;
        string directory;
        string backup_filename;
        string backup_command;

        unsigned int default_days;
        unsigned int default_weeks;
        unsigned int default_months;
        unsigned int default_years;

        unsigned int failsafe_backups;
        unsigned int failsafe_days;

        string cp_to;
        string sftp_to;

        set<string> notify;

        BackupConfig();
        ~BackupConfig();

        void saveConfig();
        bool loadConfig(string filename);
};

#endif

