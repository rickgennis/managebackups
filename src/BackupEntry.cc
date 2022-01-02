#include <stdio.h>
#include <iostream>
#include <string>
#include <math.h>
#include "BackupEntry.h"
#include <pcre++.h>
#include "util.h"
#include "colors.h"

#define SECS_PER_DAY (60*60*24)
#define DAYS_PER_MONTH 30.43

using namespace pcrepp;
using namespace std;


BackupEntry::BackupEntry() {
    dateRE = Pcre("-(20\\d{2})[-.]*(\\d{2})[-.]*(\\d{2})[-.]");
    md5 = "";
    filename = "";
    links = mtime = size = inode = day_age = month_age = dow = date_day = duration = current = 0;
}

string BackupEntry::class2string() {
    return("[" + filename + "]," + md5 + "," + to_string(links) + "," + to_string(mtime) + "," +
            to_string(size) + "," + to_string(inode) + "," + to_string(day_age) + "," +
            to_string(month_age) + "," + to_string(dow) + "," + to_string(date_day) + "," + to_string(duration));
}

void BackupEntry::string2class(string data) {
    Pcre regEx("\\[(.+)\\],([a-f0-9]{32}),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)");

    if (regEx.search(data) && regEx.matches() > 9) {
        filename = regEx.get_match(0);
        md5 = regEx.get_match(1);
        links = stoi(regEx.get_match(2));
        mtime = stoi(regEx.get_match(3));
        size = stoi(regEx.get_match(4));
        inode = stoi(regEx.get_match(5));
        day_age = stoi(regEx.get_match(6));
        month_age = stoi(regEx.get_match(7));
        dow = stoi(regEx.get_match(8));
        date_day = stoi(regEx.get_match(9));
        duration = stoi(regEx.get_match(10));
    }
    else
        log("unable to parse cache line (" + data + ")");
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
        cerr << RED << "error: invalid log filename (" << filename << ")" << RESET << endl;
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

    day_age = floor((refTime - fileMTime) / SECS_PER_DAY);
    month_age = floor(day_age / DAYS_PER_MONTH);

    auto pFileTime = localtime(&fileMTime);

    // these never change but let's set them here since we already looked them up
    dow = pFileTime->tm_wday;
    date_day = pFileTime->tm_mday;

    return this;
}


void BackupEntry::calculateMD5() {
    md5 = MD5file(filename);
}
