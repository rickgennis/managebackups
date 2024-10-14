
#include <string>
#include <filesystem>

#include "globals.h"
#include "interactive.h"
#include "FaubEntry.h"
#include "BackupConfig.h"
#include "util_generic.h"


void interactive(string mybinary) {
    BackupConfig config;
    string data;
        
    cout << "managebackups can create single-file style backups (like tar, dump, cpio) or faub-style backups.\n";
    cout << "Single file is better for collections of files where a large number of them change every time,\n";
    cout << "like log files or database files.  For anything where a large number of files stay static, faub\n";
    cout << "is more efficient.  Let's create a sample faub config to get started.  The following quesitons\n";
    cout << "will be used to populate a faub config file.  Don't worry if you change your mind about something.\n";
    cout << "The config file is text and can also be edited by hand.\n";
    
    while (1) {
        cout << BOLDBLUE << "\nWhat do you want to name this backup set? This is the profile name and it can't contain any spaces:" << RESET << endl;
        getline(cin, data);
        if (data.find(" ") == string::npos)
            break;
        cerr << "Nope, spaces aren't valid." << endl;
    }
    config.settings[sTitle].value = data;
    
    cout << BOLDBLUE << "\nWhere do you want to store the backups on this server? (directory)" << RESET << endl;
    getline(cin, data);
    config.settings[sDirectory].value = data;
    
    while (1) {
        cout << BOLDBLUE << "\nIs the data you want to backup on this server? Type local or remote." << RESET << endl;
        getline(cin, data);
        if (data == "local" || data == "remote")
            break;
    }
    
    if (data == "local") {
        cout << BOLDBLUE << "\nWhere is the data you want to backup? You can list files, directories or a combination of both.\n";
        cout << "Use a space to seperate multiple entries. And be sure to escape any spaces that are embedded within\n";
        cout << "a name with a backslash like\\ this." << RESET << endl;
        getline(cin, data);
        config.settings[sFaub].value = realpathcpp(mybinary) + " -s '" + data + "'";
    }
    else {
        cout << BOLDBLUE << "\nEnter the full ssh command to get a command line prompt on the remote system. This command needs to\n";
        cout << "run without prompting for passwords or MFA so you'll need public keys pre-configured." << RESET << endl;
        getline(cin, data);
        config.settings[sFaub].value = data;
        
        cout << BOLDBLUE << "\nEnter the full path to the managebackups binary installed on the remote server (e.g. /usr/bin/managebackups)" << RESET << endl;
        getline(cin, data);
        config.settings[sFaub].value += " " + data;
        
        cout << BOLDBLUE << "\nWhere is the data you want to backup on the remote server? You can list files, directories or a\n";
        cout << "combination of both. Use a space to seperate multiple entries. And be sure to escape any spaces that are\n";
        cout << "embedded within a name with a backslash like\\ this." << RESET << endl;
        getline(cin, data);
        config.settings[sFaub].value += " -s '" + data + "'";
    }

    while (1) {
        cout << BOLDBLUE << "\nWho you gonna call, er, what email address should be notified of failures?" << RESET << endl;
        getline(cin, data);
        if (data == "" || (data.find("@") != string::npos && data.find(" ") == string::npos))
            break;
        cout << "That doesn't look like a valid email address.\n";
    }
    config.settings[sNotify].value = data;
    
    config.config_filename = slashConcat(GLOBALS.confDir, safeFilename(config.settings[sTitle].value)) + ".conf";
    string tryConfigName = config.config_filename;

    int suffix = 0;
    while (exists(tryConfigName)) {
        tryConfigName = config.config_filename;
        tryConfigName.insert(config.config_filename.rfind("."), to_string(++suffix));
    }

    config.config_filename = tryConfigName;
    config.saveConfig();
    
    cout << BOLDBLUE << "\n\nYou're all set. A new config file has been written out to " << BOLDYELLOW << config.config_filename << BOLDBLUE << " with\n";
    cout << "everything you specified plus several defaults. Feel free to edit it. You can invoke it as is with\n";
    cout << BOLDYELLOW << "\tmanagebackups -p " << config.settings[sTitle].value << RESET << endl << endl;
}
