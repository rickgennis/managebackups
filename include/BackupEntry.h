#include <string>
using namespace std;

class BackupEntry {
    public:
    string filename;
    string md5;
    unsigned int links;
    unsigned long mtime;
    unsigned long bytes;
    unsigned long inode;
    unsigned long age;
    unsigned int month_age;
    char dow;
    char day;

    BackupEntry();
};

