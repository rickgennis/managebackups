#include "Setting.h"
#include "globals.h"

map<string, int>settingMap =
{{ CLI_PROFILE, sTitle },
    { CLI_DIR, sDirectory },
    { CLI_FILE, sBackupFilename },
    { CLI_COMMAND, sBackupCommand },
    { CLI_DAYS, sDays },
    { CLI_WEEKS, sWeeks },
    { CLI_MONTHS, sMonths },
    { CLI_YEARS, sYears },
    { CLI_FS_BACKUPS, sFailsafeBackups },
    { CLI_FS_DAYS, sFailsafeDays },
    { CLI_SCPTO, sSCPTo },
    { CLI_SFTPTO, sSFTPTo },
    { CLI_PRUNE, sPruneLive },
    { CLI_NOTIFY, sNotify },
    { CLI_MAXLINKS, sMaxLinks },
    { CLI_TIME, sIncTime },
    { CLI_NOS, sNos },
    { CLI_MINSIZE, sMinSize },
    { CLI_DOW, sDOW },
    { CLI_FS_FP, sFP },
    { CLI_MODE, sMode },
    { CLI_MINSPACE, sMinSpace },
    { CLI_MINSFTPSPACE, sMinSFTPSpace },
    { CLI_NICE, sNice },
    { CLI_TRIPWIRE, sTripwire },
    { CLI_NOTIFYEVERY, sNotifyEvery },
    { CLI_MAILFROM, sMailFrom },
    { CLI_LEAVEOUTPUT, sLeaveOutput },
    { CLI_FAUB, sFaub },
    { CLI_UID, sUID},
    { CLI_GID, sGID},
    { CLI_CONSOLIDATE, sConsolidate}
    };
// CLI_PATHS is excluded because it's only accessed as a commandline option
// and never as a Setting. And because its a vector<> that's one less special-
// case selectOrSetupConfig() needs to address.



Setting::Setting(string name, string pattern, enum SetType setType, string defaultVal) {
    regex = Pcre("(?:^|\\s)" + pattern + CAPTURE_VALUE + RE_COMMENT);
    display_name = name;
    data_type = setType;
    defaultValue = defaultVal;
    value = defaultValue;
    seen = false;
}


string Setting::confPrint(string sample) {
    bool isDef = value == defaultValue;

    return(blockp((isDef ? "#" : "") + display_name + ":", -17) +
        (isDef && sample.length() ? blockp(sample, -25) + "  # example" : 
         (blockp(data_type == BOOL ? (str2bool(value) ? "true" : "false") : value, -25) + (isDef ? "  # default" : ""))) + "\n");
}


/*
string Setting::getValue(bool getDefault) {
    string result;
    seen = true;

    return std::visit([](auto&& arg) -> string {
            using T = std::decay_t<decltype(arg)>;

            // int
            if constexpr (std::is_same_v<T, int>)
                return to_string(arg);

            // string
            else if constexpr (std::is_same_v<T, std::string>)
                return(arg);

            // vector
            else if constexpr (std::is_same_v<T, std::vector<string>>) {
                auto vec = arg;
                string result;
                for (auto vec_it = vec.begin(); vec_it != vec.end(); ++vec_it) {
                    result += (result.length() ? ", " : "") + *vec_it;
                }
                return result;
            }

            return "";  // should never get here
    }, getDefault ? defaultValue : value);
}


void Setting::value = string newValue) {
    cout << "SETTING " << display_name << " [was {" << value << "}] to " << newValue << endl;

    switch (data_type) {
        case INT: {
            int tempVal = stoi(newValue);
            value = tempVal;
            break;
        }

        case STRING:
        default:
            value = newValue;
            break;

        case VECTOR: {
            vector<string> vec;
            Pcre reDelimiters("\\s*[,;]\\s*", "g");
            auto values = reDelimiters.split(newValue);

            for (auto str_it = values.begin(); str_it != values.end(); ++str_it)
                vec.insert(vec.end(), *str_it);

            value = vec;
            break;
        }
    }
}


void Setting::value = int newValue) {   // convenience func
    cout << "SETTING (int)" << display_name << " [was {" << value << "}] to " << newValue << endl;
    assert(data_type == INT);
    value = newValue;

}
*/

