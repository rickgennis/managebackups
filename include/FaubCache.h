#ifndef FCACHE_H
#define FCACHE_H

#include <string>
#include <set>
#include <map>
#include <sys/stat.h>
#include "FaubEntry.h"
#include "tagging.h"
#include "globals.h"


using namespace std;


/* cmpName is to allow a full path filename (/var/backups/2023/01/mybackup-20230105.tgz)
 to be sorted based on the filename and not the directory.  this is helpful if the same
 profile is run with and without the --time option, resulting in some files being in
 the month directory and others being in month/day. */
struct cmpName {
    bool operator()(const string& a, const string& b) const {
        auto aps = pathSplit(a);
        auto bps = pathSplit(b);
        
        aps.file.erase(remove(aps.file.begin(), aps.file.end(), '-'), aps.file.end());
        bps.file.erase(remove(bps.file.begin(), bps.file.end(), '-'), bps.file.end());
        
        return aps.file < bps.file;
    }
};


typedef map<string, FaubEntry, cmpName>::iterator myMapIT;


class FaubCache {
private:
    string uuid;
    string baseDir;
    string coreProfile;
    map<string, FaubEntry, cmpName> backups;
    string inProcessFilename;
    
    void restoreCache_internal(string backupDir);
    myMapIT findBackup(string backupDir, myMapIT backupIT);
    
public:
    void restoreCache(string profileName);
    void restoreCache(string path, string profileName, string aUuid);
    DiskStats getTotalStats();
    long size() { return backups.size(); }
    
    unsigned long getNumberOfBackups() { return backups.size(); }
    myMapIT getBackupByDir(string dir) { return backups.find(dir); }
    myMapIT getFirstBackup() { return (backups.begin()); }
    myMapIT getLastBackup() { return (backups.size() ? --backups.end() : backups.end()); }
    myMapIT getEnd() { return backups.end(); }
    void removeBackup(map<string, FaubEntry>::iterator which) {
        //Tagging tags;
        //tags.removeTagsOn(which->first);

        GLOBALS.tags.removeTagsOn(which->first);
        which->second.removeEntry();
        backups.erase(which);
    }
    
    void updateDiffFiles(string backupDir, set<string> files);
    bool displayDiffFiles(string backupDir);
    void compare(string backupA, string backupB, string threshold);

    void tagBackup(string tagname, string backup);
    string holdBackup(string hold, string backup);

    void cleanup();
    void recache(string targetDir, time_t deletedtime = 0, bool forceAll = false);
    string getInProcessFilename() { return inProcessFilename; }
    void renameBaseDirTo(string newDir);
    string getBaseDir() { return baseDir; }
    
    FaubCache(string path, string profileName, string uuid);
    FaubCache() {}
    ~FaubCache() {}
    
    friend bool fcCleanupCallback(pdCallbackData &file);
    friend bool restoreCacheCallback(pdCallbackData &file);
};

#endif
