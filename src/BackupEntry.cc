#include "BackupEntry.h"

BackupEntry::BackupEntry() {
    md5 = "";
    filename = "";
    links = mtime = bytes = inode = age = month_age = dow = day = 0;
}
