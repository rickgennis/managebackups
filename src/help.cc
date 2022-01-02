
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

    }
}

