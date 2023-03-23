#ifndef ENTRY_H
#define ENTRY_H

#include <string>
#include <time.h>
#include <pcre++.h>

using namespace pcrepp;
using namespace std;

class BackupEntry {
    Pcre dateRE;

    public:
        bool            current;
        string          filename;
        string          md5;
        unsigned int    links;
        time_t          mtime;
        unsigned long   size;
        unsigned long   inode;
        unsigned long   duration;       // how long the backup took to run

        /* all of the below are calculated off the date in the filename (mybackup-20210102.tgz),
           *NOT* via the mtime.  This is because we'll have multiple backups from different days
           that are all hardlinked together (due to having identical content) and that means they
           share the same inode and same single mtime entry.  mtime won't show when they were taken. */

        unsigned long   fnameDayAge;
        int             dow;
        int             date_month;
        int             date_day;
        int             date_year;
        time_t          name_mtime;

    BackupEntry();

    string class2string(string oldBaseDir = "", string newBaseDir = "");
    bool string2class(string data);

    BackupEntry* updateAges(time_t refTime = 0);
    bool calculateMD5(string reason = "");
};

#endif

