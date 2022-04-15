  
#include <iostream>
#include <string>
#include <set>
#include "help.h"
#include "BackupConfig.h"
#include "PipeExec.h"
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
                sprintf(buffer, "   %-15s %s", cfg.display_name.c_str(), cfg.value.c_str());
                cout << buffer << endl;
            }
            break;
        }

        case hOptions: {
            string helpText = "managebackups [options]\n\n"
            + string(BOLDBLUE) + "EXECUTE A BACKUP" + string(RESET) + "\n"
            + "   --directory [dir]   Directory to save to and look for backups in\n"
            + "   --cmd [command]     Command to take a backup; the backup result should be written to STDOUT.\n" 
            + "   --file [filename]   The base filename to save the backup to. The date and, optionally, time will automatically be inserted.\n"
            + "   --time              Include the time in the backup filename; also inserts the day into the subdirectory.\n"
            + "   --mode [mode]       chmod newly created backups to this octatl mode (default 0600).\n"
            + "   --minsize [size]    Backups less than this size are considered failures and discarded.\n"
            + "   --scp [dest]        SCP the new backup to the destination. Dest can include user@machine:/dir/dirs.\n"
            + "   --sftp [dest]       SFTP the new backup to the destination. Dest can include SCP details plus SFTP flags (like -P for port).\n"
            + "                       SCP & SFTP also support variable interpolation of these strings which will be sustituted with values\n"
            + "                       relative to the newly created backup: {fulldir}, {subdir}, {filename}.\n"
            + "   --minspace [size]   Minimum local free space required before taking a backup\n"          
            + "   --minsftpspace [sz] Minimum free space required on the remote SFTP server before transfering a backup\n"          
            + "   --notify [contact]  Notify after a backup completes; can be email addresses and/or script names (failures only).\n"
            + "   --notifyevery [x]   Notify on every x failure (plus the first one).\n"
            + "   --nos               Notify on success also.\n"
            + "   --mailfrom [addr]   Use addr as the sending/from address for outgoing notify emails.\n"
            + "\n" + string(BOLDBLUE) + "PRUNING\n" + RESET
            + "   --days [x]          Keep x daily backups\n"
            + "   --weeks [x]         Keep x weekly backups\n"
            + "   --months [x]        Keep x monthly backups\n"
            + "   --years [x]         Keep x yearly backups\n"
            + "   --dow [x]           Day of week to save for weeklies (0=Sunday, 1=Monday, etc); defaults to Sunday.\n"
            + "   --fs_backups [b]    FAILSAFE: Require b backups before pruning\n"
            + "   --fs_days [d]       FAILSAFE: within the last d days.\n"
            + "   --fp                FAILSAFE: Paranoid mode; sets --fb=1, --fd=2\n"
            + "\n" + string(BOLDBLUE) + "HARD LINKING\n" + RESET
            + "   --maxlinks [x]      Max number of links to a file (default 20).\n"
            + "   --prune             Enable pruning.\n"
            + "   --noprune           Disable pruning.\n"
            + "\n" + string(BOLDBLUE) + "GENERAL\n" + RESET
            + "   --profile [name]    Use the specified profile for the current run.\n"
            + "   --save              Save all the specified settings to the specified profile.\n"
            + "   --recreate          Delete any existing .conf file for this profile and recreate it in the standard format\n"
            + "   -a, --all           Execute all profiles sequentially\n"
            + "   -A, --All           Execute all profiles in parallel\n"
            + "   -k, --cron          Execute all profiles sequentially for cron (equivalent to '-a -x -q')\n"
            + "   -K, --Cron          Execute all profiles in parallel for cron (equivalent to '-A -x -q')\n"
            + "   -0                  Provide a summary of backups.\n"
            + "   -1                  Provide detail of backups.\n"
            + "   --install           Install this binary in /usr/local/bin, update directory perms and create the man page.\n"
            + "   --installman        Only create and install the man page.\n"
            + "   --confdir [dir]     Use dir for the configuration directory (default /etc/managebackups).\n"
            + "   --cachedir [dir]    Use dir for the cache directory (default /var/managebackups/cache).\n"
            + "   --logdir [dir]      Use dir for the log directory (default /var/log).\n"
            + "   --user              Set directories (config, cache and log) to the calling user's home directory (~/managebackups/).\n"
            + "   --nocolor           Disable color output.\n"
            + "   --test              Run in test mode. No changes are persisted to disk except for caches.\n"
            + "   --leaveoutput       Leave command (backups, SFTP, etc) output in /tmp/managebackups_output.\n"
            + "   -q                  Quiet mode -- limit output, for use in scripts.\n"
            + "   -v[options]         Verbose debugging output. See 'man managebackups' for details.\n"
            + "   --defaults          Display the default settings for all profiles.\n"
            + "   -x, --lock          Lock the current profile for the duration of the run so only one copy can run at a time.\n"
            + "   --tripwire [string] Define tripwire files of the form 'filename: md5, filename: md5'.\n"
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
