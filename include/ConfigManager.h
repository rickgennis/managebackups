#include <set>
#include <vector>
#include <string>
#include "BackupConfig.h"

using namespace std;

struct ConfCmp {
    // sort configs alphabetically by title
    bool operator()(const BackupConfig lhs, const BackupConfig rhs) const {
        return (lhs.title) < (rhs.title);
    }
};


class ConfigManager {
    public:
        //set<BackupConfig, ConfCmp> configs;
        vector<BackupConfig> configs;
        const BackupConfig* config(string title);

        ConfigManager();
};

