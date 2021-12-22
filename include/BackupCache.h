#include <iostream>
#include <map>
#include <string>
#include <set>

using namespace std;

class BackupCache {
    map<int, BackupEntry> rawData;
    map<string, set<int> > indexByMD5;
    map<string, int> indexByFilename;

    public:
        BackupEntry* getByFilename(string filename);
        set<BackupEntry*> getByMD5(string md5);
        void addOrUpdate(BackupEntry updatedEntry);

        string size();
        string size(string md5);
        string fullDump();

        void saveCache(string filename);
        void restoreCache(string filename);
};

