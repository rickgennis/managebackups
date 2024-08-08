
#ifndef TAGGING_H
#define TAGGING_H

#include <map>
#include <vector>

using namespace std;

#define TAG_FILENAME     "/tags"
#define TAGHOLD_FILENAME "/tags.hold"


class Tagging {
    bool modified;
    bool loaded;
    
    multimap<string, string> backup2TagMap;
    multimap<string, string> tag2BackupMap;
    map<string, string> tag2Hold;  // save holdTime as a string so it can be a relative time offset or a hard date
    
    void load();

public:
    Tagging();
    ~Tagging();
    
    vector<string> backupsMatchingTag(string tag);
    vector<string> tagsOnBackup(string backup);
    string getTagsHoldTime(string tag);
    bool match(string tag, string backup);
    
    void tagBackup(string tag, string backup);
    void setTagsHoldTime(string tag, string hold);
    
    unsigned long removeTagsOn(string backup);
    unsigned long removeTag(string tag);
};

#endif


/*
mb -p laptop -t snapshot
mb -g -t snapshot

mb --last -t snapshot

mb -1 -t snapshot

mb -p laptop --hold 90

mb --hold 90 /var/backups/laptop/foo-2024-09-23
*/

