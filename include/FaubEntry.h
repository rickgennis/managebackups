#ifndef FENTRY_H
#define FENTRY_H

#include <string>
#include <set>
#include <sys/stat.h>
#include "globals.h"
#include "util_generic.h"


using namespace std;

#define SUFFIX_FAUBSTATS     "faub_stats"
#define SUFFIX_FAUBINODES    "faub_inodes"
#define SUFFIX_FAUBDIFF      "faub_diff"


class FaubEntry {
private:
    // 'directory' in FaubEntry is full.  i.e. /var/backup/2023/01/mybackup-202301@10:15:23
    string directory;
    string profile;
    string uuid;
    
public:
    bool updated;
    set<ino_t> inodes;
    DiskStats ds;
    size_t modifiedFiles;
    size_t unchangedFiles;
    size_t dirs;
    size_t slinks;
    time_t finishTime;
    char startDay;
    char startMonth;
    int startYear;
    unsigned int mtimeDayAge;
    unsigned long duration;
    int dow;
    time_t holdDate;
    
    string cacheFilename(string suffix) { return (slashConcat(GLOBALS.cacheDir, uuid, MD5string(directory)) + "." + suffix); }
    string getDir() { return directory; }
    
    int filenameDayAge();
    
    string stats2string();
    void string2stats(string& data);
    
    bool loadStats();
    void saveStats();
    
    void loadInodes();
    void unloadInodes() { inodes.clear(); };
    void saveInodes();
    
    void updateDiffFiles(set<string> files);
    bool displayDiffFiles();
    
    void renameDirectoryTo(string newDir, string baseDir);
    void removeEntry();
        
    FaubEntry& operator=(const DiskStats& stats);
    FaubEntry(string dir, string aProfile, string aUuid);
    ~FaubEntry();
};

#endif
