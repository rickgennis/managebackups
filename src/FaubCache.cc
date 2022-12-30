
#include <fstream>
#include "globals.h"
#include "FaubCache.h"
#include "BackupConfig.h"
#include "util_generic.h"

#include "FaubCache.h"


FaubCache::FaubCache(string path, bool autoSave) {
    _autoSave = autoSave;
    cacheFilename = GLOBALS.cacheDir + "/" + MD5string(path) + ".faub";
    restoreCache();
}


FaubCache::~FaubCache() {
    if (_autoSave && cacheFilename.length())
        saveCache();
}


void FaubCache::restoreCache() {
    ifstream cacheFile;

    cerr << "RESTORE FAUBCACHE..." << endl;

    cacheFile.open(cacheFilename);
    if (cacheFile.is_open()) {
        FaubEntry entry;
        string cacheData;

        try {
            getline(cacheFile, cacheData);
            entry.totalSize = stoll(cacheData);

            getline(cacheFile, cacheData);
            entry.totalSaved = stoll(cacheData);

            getline(cacheFile, cacheData);
            entry.finishTime = stol(cacheData);

            getline(cacheFile, cacheData);
            entry.duration = stol(cacheData);

            while (getline(cacheFile, cacheData)) {
                entry.inodes.insert(entry.inodes.end(), stoll(cacheData));
            }

            backups.insert(backups.end(), pair<string, FaubEntry>("foo", entry));
        }
        catch (...) {
            cerr << "caught stoll() exception" << endl;
        }

        cacheFile.close();
    }
    cerr << "RESTORE FAUBCACHE complete" << endl;
}


void FaubCache::saveCache() {
    ofstream cacheFile;

    cacheFile.open(cacheFilename);
    if (cacheFile.is_open()) {
        FaubEntry entry;

        for (auto &backup: backups) {
            cacheFile << backup.second.totalSize << "\n";
            cacheFile << backup.second.totalSaved << "\n";
            cacheFile << backup.second.finishTime << "\n";
            cacheFile << backup.second.duration << "\n";

            for (auto &inode: backup.second.inodes)
                cacheFile << inode << "\n";
        }

        cacheFile.close();
    }
}

