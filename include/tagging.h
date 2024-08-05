
#ifndef TAGGING_H
#define TAGGING_H

#include <map>
#include <vector>

using namespace std;

#define TAG_FILENAME     "/tags"


class Tagging {
    bool modified;
    bool loaded;
    
    multimap<string, string> backup2TagMap;
    multimap<string, string> tag2BackupMap;
    
    void load();

public:
    Tagging();
    ~Tagging();
    
    vector<string> backupsMatchingTag(string tag);
    vector<string> tagsOnBackup(string backup);
    bool match(string tag, string backup);
    
    void tagBackup(string tag, string backup);
    void fastTagBackup(string tag, string backup);
    
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

