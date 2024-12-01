#include <fstream>
#include <utility>
#include <functional>

#include "tagging.h"
#include "globals.h"
#include "util_generic.h"


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
        
        tfile.open(GLOBALS.cacheDir + TAGHOLD_FILENAME);
        
        if (tfile.is_open()) {
            string dataTag;
            string dataHoldTime;
            
            while (getline(tfile, dataTag) && getline(tfile, dataHoldTime)) {
                tag2Hold.insert(tag2Hold.end(), pair<string,string>(dataTag, dataHoldTime));
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
        
        tfile.open(GLOBALS.cacheDir + TAGHOLD_FILENAME);
        
        if (tfile.is_open()) {
            for (auto &tag: tag2Hold)
                tfile << tag.first << "\n" << tag.second << endl;
            
            tfile.close();
        }
    }
}


bool Tagging::tagBackup(string tag, string backup) {
    load();

    // match() check is required because a multimap allows dupes
    if (tag.length() && backup.length() && !match(tag, backup)) {
        tag2BackupMap.insert(tag2BackupMap.end(), pair<string,string>(tag, backup));
        backup2TagMap.insert(backup2TagMap.end(), pair<string,string>(backup, tag));
        
        modified = true;
        log(backup + " tagged as " + tag);
        return true;
    }
    
    return false;
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
    
    for (auto &dead: deadElements) {
        log(dead->first + " tag removed from " + dead->second);
        NOTQUIET && cout << "\t• removed " << dead->first << " tag from " << dead->second << "\n";
        tag2BackupMap.erase(dead);
    }
    
    modified = modified || deadElements.size();
    return deadElements.size();
}


unsigned long Tagging::removeTag(string tag, string profile) {
    load();
        
    string p = profile.length() ? "/" + profile + "-2" : "";
    
    vector<multimap<string, string>::iterator> deadElements;
    for (auto entry = backup2TagMap.begin(); entry != backup2TagMap.end(); ++entry) {

        // tag has to match and either no profile specified or profile matches too
        if (entry->second == tag && (!p.length() || entry->first.find(p) != string::npos))
            deadElements.insert(deadElements.end(), entry);
    }
    
    for (auto &dead: deadElements) {
        log(dead->second + " tag removed from " + dead->first);
        NOTQUIET && cout << "\t• removed tag " << tag << " from " << dead->first << "\n";
        backup2TagMap.erase(dead);
    }
    
    deadElements.clear();
    
    for (auto entry = tag2BackupMap.begin(); entry != tag2BackupMap.end(); ++entry)
        
        // tag has to match and either no profile specified or profile matches too
        if (entry->first == tag && (!p.length() || entry->second.find(p) != string::npos))
            deadElements.insert(deadElements.end(), entry);
    
    for (auto &dead: deadElements)
        tag2BackupMap.erase(dead);
    
    tag2Hold.erase(tag);
    
    modified = modified || deadElements.size();
    return deadElements.size();
}


string Tagging::listTags() {
    load();
    string result;

    // make copies so we don't alter the originals
    multimap<string,string> m1(tag2BackupMap);
    map<string,string> m2(tag2Hold);

    // merge them to a master copy
    multimap<string,string> combinedList;
    combinedList.merge(m1);
    combinedList.merge(m2);

    
    if (combinedList.size()) {
        unsigned int maxLen = 0;

        for (auto tag: combinedList)
            if (tag.first.length() > maxLen)
                maxLen = (unsigned int)tag.first.length();
        
        for (auto tag: combinedList) {
            auto holdItr = tag2Hold.find(tag.first);
            
            if (holdItr != tag2Hold.end())
                result += tag.first + string(5 + maxLen - tag.first.length(), ' ') + "[" + holdItr->second + "]\n";
            else
                result += tag.first + "\n";
        }
    }
    else
        result = "no tags are defined.\n";

    return result;
}


bool Tagging::match(string tag, string backup) {
    if (tag.length()) {
        load();
        
        for (auto [start, end] = backup2TagMap.equal_range(backup); start != end; ++start)
            if (start->second == tag)
                return true;
    }
    
    return false;
}


void Tagging::setTagsHoldTime(string tag, string hold) {
    load();
    
    if (tag.length()) {
        tag2Hold.erase(tag);

        if (hold.length() && hold != "0") {
            tag2Hold.insert(tag2Hold.end(), pair<string, string>(tag, hold));
            log("tag " + tag + " mapped to " + (hold == "::" ? "permanent" : hold) + " hold");
        }
        else
            log("hold time mapping removed from tag " + tag);
        
        modified = true;
    }
}


string Tagging::getTagsHoldTime(string tag) {
    load();
    
    if (tag.length()) {
        auto t = tag2Hold.find(tag);
        if (t != tag2Hold.end())
            return(t->second);
    }
    
    return "";
}


void Tagging::renameProfile(string oldBaseDir, string newBaseDir) {
    load();
    
    multimap<string, string> newMMap;
    for (auto entry = tag2BackupMap.begin(); entry != tag2BackupMap.end(); ++entry)
        newMMap.insert(newMMap.end(), make_pair(searchreplace(oldBaseDir, entry->first, newBaseDir), searchreplace(oldBaseDir, entry->second, newBaseDir)));
    tag2BackupMap = newMMap;

    newMMap.clear();
    for (auto entry = backup2TagMap.begin(); entry != backup2TagMap.end(); ++entry)
        newMMap.insert(newMMap.end(), make_pair(searchreplace(oldBaseDir, entry->first, newBaseDir), searchreplace(oldBaseDir, entry->second, newBaseDir)));
    backup2TagMap = newMMap;

    map<string, string> newMap;
    for (auto entry = tag2Hold.begin(); entry != tag2Hold.end(); ++entry)
        newMap.insert(newMap.end(), make_pair(searchreplace(oldBaseDir, entry->first, newBaseDir), searchreplace(oldBaseDir, entry->second, newBaseDir)));
    tag2Hold = newMap;
    
    modified = true;
    // Tagging destructor will write this back to disk
}
