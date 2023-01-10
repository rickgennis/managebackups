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


class FaubEntry {
    private:
        // 'directory' in FaubEntry is full.  i.e. /var/backup/2023/01/mybackup-202301@10:15:23
        string directory;

    public:
        bool updated;
        set<ino_t> inodes;
        DiskStats ds;
        //size_t sizeInBytes;
        //size_t sizeInBlocks;
        //size_t savedInBytes;
        //size_t savedInBlocks;
        size_t modifiedFiles;
        size_t unchangedFiles;
        size_t dirs;
        size_t slinks;
        time_t finishTime;
        unsigned long duration;

        string cacheFilename(string suffix) { return (GLOBALS.cacheDir + "/" + MD5string(directory) + "." + suffix); }

        string stats2string();
        void string2stats(string& data);
        
        bool loadStats();
        void saveStats();

        void loadInodes();
        void saveInodes();

        FaubEntry& operator=(const DiskStats& stats);
        FaubEntry(string dir);
        ~FaubEntry();
};

#endif
