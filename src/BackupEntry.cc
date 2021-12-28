#include "BackupEntry.h"
#include <stdio.h>
#include <iostream>
#include <string>
#include <math.h>
#include <pcre++.h>
#include "util.h"

#define SECS_PER_DAY (60*60*24)
#define DAYS_PER_MONTH 30.43

using namespace pcrepp;
using namespace std;


BackupEntry::BackupEntry() {
    md5 = "";
    filename = "";
    links = mtime = size = inode = day_age = month_age = dow = date_day = 0;
}

string BackupEntry::class2string() {
    return("[" + filename + "]," + md5 + "," + to_string(links) + "," + to_string(mtime) + "," +
            to_string(size) + "," + to_string(inode) + "," + to_string(day_age) + "," +
            to_string(month_age) + "," + to_string(dow) + "," + to_string(date_day));
}

void BackupEntry::string2class(string data) {
    Pcre regEx("\\[(.+)\\],([a-f0-9]+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)");

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
    }
    else
        log("unable to parse cache line (" + data + ")");
}


BackupEntry* BackupEntry::updateAges(time_t refTime) {
    if (!refTime)
        time(&refTime);

    struct tm *tM = localtime(&mtime);
    struct tm fileTime = *tM;

    tM = localtime(&refTime);
    struct tm nowTime = *tM;
    
    day_age = floor((refTime - mtime) / SECS_PER_DAY);

    if ((fileTime.tm_mday != nowTime.tm_mday) ||
            (fileTime.tm_mon != nowTime.tm_mon) || 
            (fileTime.tm_year != nowTime.tm_year))
        ++day_age;

    month_age = floor(day_age / DAYS_PER_MONTH);

    // these never change but let's set them here since we already looked them up
    dow = fileTime.tm_wday;
    date_day = fileTime.tm_mday;

    return this;
}


void BackupEntry::calculateMD5() {
    md5 = MD5file(filename);
}
