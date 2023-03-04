  
#include <iostream>
#include <string>
#include <set>
#include "help.h"
#include "BackupConfig.h"
#include "ipc.h"
#include "globals.h"
#include "util_generic.h"


using namespace std;

void showHelp(enum helpType kind) {
    switch (kind) {
        case hDefaults: {
            BackupConfig config;
            cout << "Configuration defaults:" << endl;

            char buffer[200];
            for (auto cfg: config.settings) {
                snprintf(buffer, sizeof(buffer), "   %-15s %s", cfg.display_name.c_str(), cfg.value.c_str());
                cout << buffer << endl;
            }
            break;
        }

        case hOptions: {
            string helpText = "managebackups [options]\n\n"
            + string(BOLDBLUE) + "EXECUTE A BACKUP (single file)" + string(RESET) + "\n"
            + "   --cmd [command]     Command to take a single-file backup; the backup result should be written to STDOUT.\n" 
            + "   --file [filename]   The base filename to save the backup to. The date and, optionally, time will automatically be inserted.\n"
            + "   --mode [mode]       chmod newly created backups to this octal mode (default 0600).\n"
            + "   --uid [uid]         chown newly created backups to this numeric UID.\n"
            + "   --gid [gid]         chgrp newly created backups to this numeric GID.\n"
            + "   --minsize [size]    Single-file backups less than this size are considered failures and discarded.\n"
            + "   --minspace [size]   Minimum local free space required before taking a backup\n"          
            + "   --scp [dest]        SCP the new backup to the destination. Dest can include user@machine:/dir/dirs.\n"
            + "   --sftp [dest]       SFTP the new backup to the destination. Dest can include SCP details plus SFTP flags (like -P for port).\n"
            + "                       SCP & SFTP also support variable interpolation of these strings which will be sustituted with values\n"
            + "                       relative to the newly created backup: {fulldir}, {subdir}, {filename}.\n"
            + "   --minsftpspace [sz] Minimum free space required on the remote SFTP server before transfering a backup\n\n"
            + string(BOLDBLUE) + "EXECUTE A BACKUP (faub-style)" + string(RESET) + "\n"
            + "   --faub [command]    Command to take a faub-style backup; managebackups will be required on the remote server as well.\n\n"
            + string(BOLDBLUE) + "EXECUTE A BACKUP (both)" + string(RESET) + "\n"
            + "   --directory [dir]   Directory to save to and look for backups in\n"
            + "   --time              Include the time in the backup filename; also inserts the day into the subdirectory.\n"
            + "   --notify [contact]  Notify after a backup completes; can be email addresses and/or script names (failures only).\n"
            + "   --notifyevery [x]   Notify on every x failure (plus the first one).\n"
            + "   --nos               Notify on success also.\n"
            + "   --mailfrom [addr]   Use addr as the sending/from address for outgoing notify emails.\n"
            + "\n" + string(BOLDBLUE) + "PRUNING\n" + RESET
            + "   --prune             Enable pruning.\n"
            + "   --noprune           Disable pruning.\n"
            + "   --days [x]          Keep x daily backups\n"
            + "   --weeks [x]         Keep x weekly backups\n"
            + "   --months [x]        Keep x monthly backups\n"
            + "   --years [x]         Keep x yearly backups\n"
            + "   --dow [x]           Day of week to save for weeklies (0=Sunday, 1=Monday, etc); defaults to Sunday.\n"
            + "   --consolidate [x]   Prune down to a single backups of the profilel per day after its x days old.\n"
            + "   --fs_backups [b]    FAILSAFE: Require b backups before pruning\n"
            + "   --fs_days [d]       FAILSAFE: within the last d days.\n"
            + "   --fp                FAILSAFE: Paranoid mode; sets --fs_backups 1 --fs_days 2\n"
            + "\n" + string(BOLDBLUE) + "HARD LINKING\n" + RESET
            + "   -l, --maxlinks [x]  Max number of links to a file (default 100).\n"
            + "\n" + string(BOLDBLUE) + "GENERAL\n" + RESET
            + "   -p, --profile [p]   Use the specified profile for the current run; can be a partial name\n"
            + "   --save              Save all the specified settings to the specified profile.\n"
            + "   --recreate          Delete any existing .conf file for this profile and recreate it in the standard format\n\n"
            + "   --diff [bkup]       List files that changed in the specified backup\n"
            + "   --diffl [bkup]      List files that changed in the specified backup in the context of that backup (long form)\n\n"
            + "   -a, --all           Execute all profiles sequentially\n"
            + "   -A, --All           Execute all profiles in parallel\n"
            + "   -k, --cron          Execute all profiles sequentially for cron (equivalent to '-a -x -q')\n"
            + "   -K, --Cron          Execute all profiles in parallel for cron (equivalent to '-A -x -q')\n\n"
            + "   -0                  Provide a summary of backups; can be combined with -p to limit output\n"
            + "   -1                  Provide detail of backups; can be combined with -p to limit output\n\n"
            + "   --install           Install this binary in /usr/local/bin, update directory perms and create the man page\n"
            + "   --installsuid       Install this binary in /usr/local/bin with SUID to run as root; create the man page\n"
            + "   --installman        Only create and install the man page\n\n"
            + "   --confdir [dir]     Use dir for the configuration directory (default /etc/managebackups)\n"
            + "   --cachedir [dir]    Use dir for the cache directory (default /var/managebackups/cache)\n"
            + "   --logdir [dir]      Use dir for the log directory (default /var/log)\n"
            + "   --user              Set directories (config, cache and log) to the calling user's home directory (~/managebackups/)\n\n"
            + "   --nocolor           Disable color output\n"
            + "   --test              Run in test mode. No changes are persisted to disk except for caches\n"
            + "   --leaveoutput       Leave command (backup execution, SFTP, etc) output in /tmp/managebackups_output\n"
            + "   -q                  Quiet mode -- limit output, for use in scripts.\n"
            + "   -v[options]         Verbose debugging output. See 'man managebackups' for details\n"
            + "   --defaults          Display the default settings for all profiles\n"
            + "   -x, --lock          Lock the current profile for the duration of the run so only one copy can run at a time\n"
            + "   --force             Override any existing lock and force the backup to start\n"
            + "   --tripwire [string] Define tripwire files of the form 'filename: md5, filename: md5'\n"
            + "\n" + string(BOLDBLUE) + "SCHEDULING (via cron on Linux and launchctl on MacOS)\n" + RESET
            + "   --sched [h]         Schedule managebackups to run every h hours\n"
            + "   --schedhour [h]     If scheduled for once a day (--sched 24), specify which hour to run on; defaults to 0\n"
            + "   --schedminute [m]   Specify which minute to run on; defaults to 15\n"
            + "   --schedpath [p]     If managebackups isn't installed in /usr/local/bin, specify its altnernative location\n"
            + "\nSee 'man managebackups' for more detail.\n";

            /*
            // find a pager
            string pagerBin = locateBinary("less");
            if (!pagerBin.length()) {
                pagerBin = locateBinary("more");
                GLOBALS.color = false;
            }
            else
                pagerBin += " -R";  // 'less' can do color

            if (pagerBin.length()) {
                PipeExec pager(pagerBin);

                pager.execute("", true);
                pager.writeProc(helpText.c_str(), helpText.length());
                pager.closeWrite();
                wait(NULL);
            }
            else
            */
                cout << helpText;
        }

        break;

        case hSyntax:
        default:
            cout << R"END(managebackups performs backups and/or manages existing backups. Managing consists
of deleting previous backups via a defined retention schedule (keep the configured
number of daily, weekly, monthly, yearly copies) as well as hard linking backups
that have identical content together to save disk space.

    • Use "managebackups --help" for options.

    • See "man managebackups" for full detail. 

    • Use "sudo managebackups --install" to create the man page if it doesn't exist yet.)END" << endl;
        break;
    }
}


string manPathContent() {
