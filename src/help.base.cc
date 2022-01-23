 
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
            + "   --file [filename]   The base filename to save the backup to. The date and/or time will automatically be inserted.\n"
            + "   --install           Install this binary in /usr/local/bin, update directory perms and create the man page.\n"
            + "\n" + string(BOLDBLUE) + "PRUNING\n" + RESET
            + "   --daily [x]         keep x daily backups\n"
            + "   --weekly [x]        keep x weekly backups\n"
            + "   --monthly [x]       keep x monthly backups\n"
            + "   --yearly [x]        keep x yearly backups\n"
            + "   --dow [x]           day of week to save for weeklies (0=Sunday, 1=Monday, etc). defaults to Sunday.\n"
            + "\n" + string(BOLDBLUE) + "GENERAL\n" + RESET
            + "   --profile [name]    Use the specified profile for the current run.\n"
            + "   --save              Save all the specified settings to the specified profile.\n"
            + "   --notify [contact]  Notify after a backup completes; can be email addresses and/or script names (failures only).\n"
            + "   --nos               Notify on success also.\n"
            + "   --minsize [size]    Backups less than size-bytes are considered failures and discarded.\n"
            + "   --test              Run in test mode. No changes are persisted to disk except for caches.\n"
            + "   -0                  Provide a summary of backups.\n"
            + "   -1                  Provide detail of backups.\n";

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
                cout << helpText;
        }

        break;

        case hSyntax:
        default:
            cout << R"END(managebackups performs backups and/or manages existing backups. Managing consists
of deleting previous backups via a defined retention schedule (keep the configured
number of daily, weekly, monthly, yearly copies) as well as hard linking backups
that have identical content together to save disk space. See "man managebackups"
for full detail. Use "sudo managebackups --install" to create the man page if it
doesn't exist yet.)END" << endl;
        break;
    }
}


string manPathContent() {
