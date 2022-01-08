
#include <iostream>
#include <string>
#include <set>
#include "help.h"
#include "BackupConfig.h"

using namespace std;

void showHelp(enum helpType kind) {
    switch (kind) {
        case hDefaults: {
            BackupConfig config;
            cout << "Configuration defaults:" << endl;

            char buffer[200];
            for (auto set_it = config.settings.begin(); set_it != config.settings.end(); ++set_it) {
                sprintf(buffer, "\t%-15s %s", set_it->display_name.c_str(), set_it->value.c_str());
                cout << buffer << endl;
            }
        }

        case hSyntax:
        default:
            cout << R"END(manage_backups performs 3 functions.  They can be run individually or all together.  Any function that
is elected to run will happen in this order:

1. EXECUTE A BACKUP$
Take a local or remote backup.  This step can be skipped if another tool is used for backup and
manage_backups is only used for its other functions.  If elected, the string passed to the
--cmd parameter is executed and the output is written to a filename constructed from
--filename in --directory with the current YEARMONTH inserted into the filename as well as the
subdirectory.  For example, with:
    --directory /var/backup
    --filename system-backup.tgz
    --cmd "ssh remotebox tar czf - /opt"
manage_backup will create /var/backup/202104/system-backup-20210417.tgz.  Multiple backups of the
same data (same --dir, --file, --cmd) on the same day will overwrite each other, keeping only the
most recent.  To keep all copies use --time which will insert the time into the filename and the
day into the subidrectory.  If --cmd is omitted taking a backup is skipped entirely.

2. UPDATE HARD LINKS
Scan backups and replace identical copies with hard links.  The --directory
is scanned.  Files are selected by:
    If --filename is specified an exact match (minus the date) is used.
    Otherwise all files in --directory are considered.

3. PRUNE BACKUPS
Delete backups older than the specified timeframe.  Use --daily, --weekly & --monthly
to specify the timeframes.  Use -sp to skip pruning.

Use --help to see all options.
Use --examples to see example invocations.)END" << endl;

    }
}

