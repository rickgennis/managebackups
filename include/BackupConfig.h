
#include <set>
#include <string>
using namespace std;

class BackupConfig {
    public:
        string title;
        string directory;
        string filename;
        string backup_command;

        unsigned int default_days;
        unsigned int default_weeks;
        unsigned int default_months;
        unsigned int default_years;

        unsigned short failsafe_backups;
        unsigned short failsafe_days;

        string cp_to;
        string sftp_to;

        set<string> notify;

        BackupConfig();
};
