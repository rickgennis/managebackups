#ifndef FCACHE_H
#define FCACHE_H

#include <string>
#include <set>
#include <map>
#include <sys/stat.h>
#include "FaubEntry.h"


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


class FaubCache {
private:
    string baseDir;
    map<string, FaubEntry, cmpName> backups;
    string inProcessFilename;
    
public:
    void restoreCache(string profileName);
    DiskStats getTotalStats();
    long size() { return backups.size(); }
    
    unsigned long getNumberOfBackups() { return backups.size(); }
    map<string, FaubEntry>::iterator getBackupByDir(string dir) { return backups.find(dir); }
    map<string, FaubEntry>::iterator getFirstBackup() { return (backups.begin()); }
    map<string, FaubEntry>::iterator getLastBackup() { return (backups.size() ? --backups.end() : backups.end()); }
    map<string, FaubEntry>::iterator getEnd() { return backups.end(); }
    
    void updateDiffFiles(string backupDir, set<string> files);
    void displayDiffFiles(string backupDir);
    
    void cleanup();
    void recache(string dir);
    string getInProcessFilename() { return inProcessFilename; }
    
    FaubCache(string path, string profileName);
    ~FaubCache();
};

#endif
