 
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
            for (auto cfg: config.settings) {
                sprintf(buffer, "   %-15s %s", cfg.display_name.c_str(), cfg.value.c_str());
                cout << buffer << endl;
            }
            break;
        }

        case hOptions:
            cout << "managebackups [options]\n\n";
            cout << BOLDBLUE << "GENERAL" << RESET << "\n";
            cout << "   --install           Install this binary in /usr/local/bin, update directory perms and create the man page.\n";
            cout << "   --profile [name]    Use the specified profile for the current run.\n";
            cout << "   --save              Save all the specified settings to the specified profile.\n";
            cout << "   --notify [contact]  Notify after a backup completes; can be email addresses and/or script names (failures only).\n";
            cout << "   --nos               Notify on success also.\n";
            cout << "   --minsize [size]    Backups less than size-bytes are considered failures and discarded.\n";
            cout << "   --test              Run in test mode. No changes are persisted to disk except for caches.\n";
            cout << "   -0                  Provide a summary of backups.\n";
            cout << "   -1                  Provide detail of backups.\n";

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
