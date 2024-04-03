
#include <fstream>

#include "FastCache.h"
#include "util_generic.h"
#include "globals.h"

#define FC_TEXT   "tx"
#define FC_FILES  "fl"


string fastCacheFilename(string suffix) {
    return slashConcat(GLOBALS.cacheDir, "status." + suffix);
}


bool FastCache::verifyFileMtimes() {
    ifstream cacheFile;
    bool success = true;

    cacheFile.open(fastCacheFilename(FC_FILES));
    if (cacheFile.is_open()) {
        struct stat statData;
        string mtimeString;
        string filename;
        
        while (success && !cacheFile.eof()) {
            getline(cacheFile, filename);
            getline(cacheFile, mtimeString);
            
            try {
                long savedMtime = stol(mtimeString);   // throws on parse error
                
                if (filename.length() && savedMtime) {
                    success = false;
                    
                    if (!stat(filename.c_str(), &statData) && (statData.st_mtime == savedMtime))
                        success = true;
                }
            }
            catch (...) { success = false; }
        }
        
        cacheFile.close();
    }
    
    return success;
}


string FastCache::get() {
    ifstream cacheFile;
    string result;

    // if we've already done the get() and cached the output, return it.
    // if the cached output is blank and the file mtimes fail to verify,
    // return the cached output (i.e. blank), as that will show an empty cache.
    if (cachedOutput.length() || !verifyFileMtimes())
        return cachedOutput;
        
    cacheFile.open(fastCacheFilename(FC_TEXT));
    
    if (cacheFile.is_open()) {
        string tempString;
        string lineText;
        time_t firstTime;
        time_t lastTime;
        
        try {
            while (!cacheFile.eof()) {
                getline(cacheFile, lineText);
                
                if (lineText.length()) {
                    getline(cacheFile, tempString);
                    firstTime = stol(tempString);
                    
                    getline(cacheFile, tempString);
                    lastTime = stol(tempString);
                    
                    
                    if (!firstTime && lastTime == 1)
                        result += lineText + "\n";
                    else
                        result += lineText + BOLDBLUE + "[" + RESET +
                        (firstTime || lastTime ? timeDiff(mktimeval(firstTime)) +
                         BOLDBLUE + " -> " + RESET + timeDiff(mktimeval(lastTime)) : "-") + BOLDBLUE + "]" + RESET + "\n";
                }
            }
        }
        catch (...) {
            result = "";
        }
        cacheFile.close();
                
        cachedOutput = result;
    }
    
    return result;
}


void FastCache::appendStatus(FASTCACHETYPE data) {
    cachedData.insert(cachedData.end(), data);
}


void FastCache::appendFile(string filename) {
    cachedFiles.insert(cachedFiles.end(), filename);
}


void FastCache::commit() {
    ofstream cacheFile;
    string tempFilename = fastCacheFilename(FC_TEXT) + ".tmp." + to_string(getpid());

    cacheFile.open(tempFilename);
    
    if (cacheFile.is_open()) {
        for (auto &[lineText, firstTime, lastTime]: cachedData) {
            cacheFile << lineText << endl;
            cacheFile << firstTime << endl;
            cacheFile << lastTime << endl;
        }
    
        cacheFile.close();
        
        unlink(fastCacheFilename(FC_TEXT).c_str());
        if (!rename(tempFilename.c_str(), fastCacheFilename(FC_TEXT).c_str())) {
            ofstream cacheFile2;
            tempFilename = fastCacheFilename(FC_FILES) + ".tmp." + to_string(getpid());
            
            cacheFile2.open(tempFilename);
            if (cacheFile2.is_open()) {
                
                // dedupe
                sort(cachedFiles.begin(), cachedFiles.end());
                cachedFiles.erase(unique(cachedFiles.begin(), cachedFiles.end()), cachedFiles.end());
                
                for (auto &filename: cachedFiles) {
                    struct stat statData;
                    
                    if (!mystat(filename, &statData)) {
                        cacheFile2 << filename << endl;
                        cacheFile2 << statData.st_mtime << endl;
                    }
                }
                cacheFile2.close();
                
                if (rename(tempFilename.c_str(), fastCacheFilename(FC_FILES).c_str()))
                    log("error: unable to write to " + fastCacheFilename(FC_FILES));
            }
        }
        else
            log("error: unable to write to " + fastCacheFilename(FC_TEXT));
    }
}
