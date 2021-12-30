#ifndef ENTRY_H
#define ENTRY_H

#include <string>
#include <time.h>

using namespace std;

class BackupEntry {
    public:
        string          filename;
        string          md5;
        unsigned int    links;
        time_t          mtime;
        unsigned long   size;
        unsigned long   inode;
        unsigned long   day_age;
        unsigned int    month_age;
        int             dow;
        int             date_day;
        unsigned long   duration;

    BackupEntry();

    string class2string();
    void string2class(string data);

    BackupEntry* updateAges(time_t refTime = 0);
    void calculateMD5();
};

#endif

