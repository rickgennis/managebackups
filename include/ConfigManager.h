#include <vector>
#include <string>
#include "BackupConfig.h"

using namespace std;

/*struct ConfCmp {
    // sort configs alphabetically by title
    bool operator()(const BackupConfig lhs, const BackupConfig rhs) const {
        return (lhs.title) < (rhs.title);
    }
};
*/


class ConfigManager {
    public:
        int activeConfig;
        vector<BackupConfig> configs;
        int config(string title);
        void fullDump();

        ConfigManager();
};

