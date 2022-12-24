#ifndef FENTRY_H
#define FENTRY_H

#include <string>
#include <set>
#include <sys/stat.h>


using namespace std;

class FaubEntry {
    private:
        bool _autoSave;

    public:
        set<ino_t> inodes;
        size_t totalSize;
        size_t totalSaved;
        time_t finishTime;
        unsigned long duration;

        FaubEntry();
        ~FaubEntry();
};

#endif
