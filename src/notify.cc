
#ifndef NOTIFY_C
#define NOTIFY_C

#include <string>
#include <vector>

#include "util_generic.h"
#include "BackupConfig.h"
#include "PipeExec.h"

using namespace std;


void notify(BackupConfig& config, string message, bool currentSuccess, bool alwaysOverride) {
    if (!config.settings[sNotify].value.length())
        return;

    stringstream tokenizer(config.settings[sNotify].value);
    vector<string> parts;

    // separate the contacts list by commas
    string tempStr;
    while (getline(tokenizer, tempStr, ','))
        parts.push_back(trimSpace(tempStr));

    for (auto contactMethod: parts) {
        DEBUG(2, "method: " << contactMethod);

        // if there's an at-sign treat it as an email address
        if (contactMethod.find("@") != string::npos) {

            // for email send the notification if it's a failure or
            // if sNos (notify on success) is enabled
            if (alwaysOverride || str2bool(config.settings[sNos].value) || !currentSuccess)  {

                // superfluous check as test mode bombs out long before anything can call notify()
                // but just in case future logic changes, testing here
                if (GLOBALS.cli.count(CLI_TEST))
                    cout << YELLOW << config.ifTitle() << " TESTMODE: would have sent email to " << contactMethod << RESET << endl;
                else {
                    DEBUG(2, "recipient: " << contactMethod << "; sending email");
                    sendEmail(contactMethod, "managebackups - " + config.settings[sTitle].value + (currentSuccess ? " (success)" : " (failed)"), message);
                }
            }
        }
        // if there's no at-sign treat it as a script to execute
        else {
            // for script notifications  only execute if there's a state change
            // i.e. current success != previous success
            bool previousSuccess = config.getPreviousSuccess();
            DEBUG(2, "states change: prev " << previousSuccess << "; current " << currentSuccess);
            if (alwaysOverride || currentSuccess != previousSuccess) {
                config.setPreviousSuccess(currentSuccess);

                // for scripts execute the notification if it's a failure or
                // if sNos (notify on success) is enabled
                if (alwaysOverride || str2bool(config.settings[sNos].value) || !currentSuccess)  {
                    // superfluous check as test mode bombs out long before anything can call notify()
                    // but just in case future logic changes, testing here
                    if (GLOBALS.cli.count(CLI_TEST))
                        cout << YELLOW << config.ifTitle() << " TESTMODE: would have executed notify script " << contactMethod << RESET << endl;
                    else {
                        DEBUG(2, "script: " << contactMethod << "; executing");
                        if (system(string(contactMethod + " '" + message + "'").c_str()))
                            log("unable to notify via " + contactMethod + " (cannot execute)");            
                    }
                }
            }
        }
    }
}

#endif


