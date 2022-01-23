#include <stdio.h>
#include <iostream>
#include <string>
#include <math.h>
#include "BackupEntry.h"
#include <pcre++.h>
#include "util_generic.h"
#include "colors.h"

#define SECS_PER_DAY (60*60*24)
#define DAYS_PER_MONTH 30.43

using namespace pcrepp;
using namespace std;


BackupEntry::BackupEntry() {
    dateRE = Pcre(DATE_REGEX);
    md5 = filename = "";
    links = mtime = size = inode = day_age = month_age = dow = date_day = duration = current = name_mtime = 0;
}

string BackupEntry::class2string() {
    return("[" + filename + "]," + md5 + "," + to_string(links) + "," + to_string(mtime) + "," +
            to_string(size) + "," + to_string(duration));
}

void BackupEntry::string2class(string data) {
    Pcre regEx("\\[(.+)\\],([a-f0-9]{32}),(\\d+),(\\d+),(\\d+),(\\d+)");

    try {
        if (regEx.search(data) && regEx.matches() > 4) {
            filename = regEx.get_match(0);
            md5 = regEx.get_match(1);
            links = stoi(regEx.get_match(2));
            mtime = stol(regEx.get_match(3));
            size = stol(regEx.get_match(4));
            duration = stol(regEx.get_match(5));
        }
        else
            log("unable to parse cache line (" + data + ")");
    }
    catch (...) {
        SCREENERR("error: cannot parse config data: " << data);
    }
}


BackupEntry* BackupEntry::updateAges(time_t refTime) {
    if (!refTime)
        time(&refTime);

    // identical backups from different days that we've hardlinked together will all share the 
    // same mtime (single inode).  so mtime is useless for calculating the age.  we have to rely
    // on the date format that's near the end of the filename.
    
    if (dateRE.search(filename) && dateRE.matches() > 2) {
        date_year  = stoi(dateRE.get_match(0));
        date_month = stoi(dateRE.get_match(1));
        date_day   = stoi(dateRE.get_match(2));
    }
    else {   // should never get here due to a similar regex limiting filenames getting initially added to the cache
        SCREENERR("error: invalid log filename (" << filename << ")");
        exit(1);
    }

    struct tm fileTime;
    fileTime.tm_sec  = 0;
    fileTime.tm_min  = 0;
    fileTime.tm_hour = 0;
    fileTime.tm_mday = date_day;
    fileTime.tm_mon  = date_month - 1;
    fileTime.tm_year = date_year - 1900;
    fileTime.tm_isdst = -1;

    auto fileMTime = mktime(&fileTime);

    name_mtime = fileMTime;
    day_age = floor((refTime - fileMTime) / SECS_PER_DAY);
    month_age = floor(day_age / DAYS_PER_MONTH);

    auto pFileTime = localtime(&fileMTime);
    dow = pFileTime->tm_wday;
    date_day = pFileTime->tm_mday;

    return this;
}


void BackupEntry::calculateMD5() {
    md5 = MD5file(filename, !NOTQUIET);
}
