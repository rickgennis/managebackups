#include <fstream>

#include "tagging.h"
#include "globals.h"


Tagging::Tagging() {
    loaded = modified = false;
}


void Tagging::load() {
    if (!loaded) {
        loaded = true;

        fstream tfile;
        tfile.open(GLOBALS.cacheDir + TAG_FILENAME);
        
        if (tfile.is_open()) {
            string dataBackup;
            string dataTag;
            
            while (getline(tfile, dataBackup) && getline(tfile, dataTag)) {
                // match() check required because fastTagBackup may have created dupes
                if (!match(dataTag, dataBackup)) {
                    tag2BackupMap.insert(tag2BackupMap.end(), pair<string,string>(dataTag, dataBackup));
                    backup2TagMap.insert(backup2TagMap.end(), pair<string,string>(dataBackup, dataTag));
                }
                else
                    modified = true;
            }
            
            tfile.close();
        }
    }
}


Tagging::~Tagging() {
    if (modified) {
        ofstream tfile;
        tfile.open(GLOBALS.cacheDir + TAG_FILENAME);
        
        if (tfile.is_open()) {
            for (auto &tag: backup2TagMap)
                tfile << tag.first << "\n" << tag.second << endl;
            
            tfile.close();
        }
    }
}


void Tagging::fastTagBackup(string tag, string backup) {
    if (loaded)
        tagBackup(tag, backup);
    else {
        ofstream tfile;
        
        tfile.open(GLOBALS.cacheDir + TAG_FILENAME, std::ios::app);
        if (tfile.is_open()) {
            tfile << backup << "\n" << tag << "\n";
            tfile.close();
        }
    }
}


void Tagging::tagBackup(string tag, string backup) {
    load();

    // match() check is required because a multimap would allow dupes
    if (tag.length() && backup.length() && !match(tag, backup)) {
        tag2BackupMap.insert(tag2BackupMap.end(), pair<string,string>(tag, backup));
        backup2TagMap.insert(backup2TagMap.end(), pair<string,string>(backup, tag));
        
        modified = true;
    }
}


vector<string> Tagging::backupsMatchingTag(string tag) {
    load();
    
    vector<string> result;
    for (auto [start, end] = tag2BackupMap.equal_range(tag); start != end; ++start)
        result.insert(result.end(), start->second);
    
    return result;
}


vector<string> Tagging::tagsOnBackup(string backup) {
    load();
    
    vector<string> result;
    for (auto [start, end] = backup2TagMap.equal_range(backup); start != end; ++start)
        result.insert(result.end(), start->second);
    
    return result;
}


unsigned long Tagging::removeTagsOn(string backup) {
    load();

    vector<multimap<string, string>::iterator> deadElements;
    backup2TagMap.erase(backup);
        
    for (auto entry = tag2BackupMap.begin(); entry != tag2BackupMap.end(); ++entry)
        if (entry->second == backup)
            deadElements.insert(deadElements.end(), entry);
    
    for (auto &dead: deadElements)
        tag2BackupMap.erase(dead);
    
    modified = modified || deadElements.size();
    return deadElements.size();
}


unsigned long Tagging::removeTag(string tag) {
    load();
    
    vector<multimap<string, string>::iterator> deadElements;
    tag2BackupMap.erase(tag);
    
    for (auto entry = backup2TagMap.begin(); entry != backup2TagMap.end(); ++entry)
        if (entry->second == tag)
            deadElements.insert(deadElements.end(), entry);
    
    for (auto &dead: deadElements)
        backup2TagMap.erase(dead);

    modified = modified || deadElements.size();
    return deadElements.size();
}


bool Tagging::match(string tag, string backup) {
    load();
    
    for (auto [start, end] = backup2TagMap.equal_range(backup); start != end; ++start)
        if (start->second == tag)
            return true;
    
    return false;
}
