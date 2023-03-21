#ifndef BCACHE_H
#define BCACHE_H

#include <iostream>
#include <map>
#include <string>
#include <set>

#include "BackupEntry.h"
#include "FaubCache.h"   // for cmpName


using namespace std;

class BackupCache {
    string cacheFilename;

public:
    map<unsigned int, BackupEntry> rawData;
    map<string, set<unsigned int> > indexByMD5;
    map<string, unsigned int, cmpName> indexByFilename;
    bool updated;
    string inProcess;
    
    BackupEntry* getByFilename(string filename);
    set<BackupEntry*> getByMD5(string md5);
    void addOrUpdate(BackupEntry updatedEntry, bool markCurrent = false, bool md5Updated = false);
    void updateAges(time_t refTime = 0);
    void reStatMD5(string md5);
    void remove(BackupEntry oldEntry);
    
    string size();
    string size(string md5);
    string fullDump();
    
    void setCacheFilename(string filename) { cacheFilename = filename + ".1f"; }
    void saveCache(string oldBaseDir = "", string newBaseDir = "");  // only specify base dirs when relocating
    bool restoreCache(bool nukeFirst = false);
    void saveAtEnd(bool shouldSave = 1) { updated = shouldSave; }
    
    void cleanup();
    
    BackupCache(string filename);
    BackupCache();
    ~BackupCache();
};

#endif

