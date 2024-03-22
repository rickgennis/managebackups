
#include <fstream>

#include "FastCache.h"
#include "util_generic.h"
#include "globals.h"


string fastCacheFilename() {
    return slashConcat(GLOBALS.cacheDir, "status.fc");
}


string FastCache::get() {
    ifstream cacheFile;
    string result;
    
    if (cachedOutput.length())
        return cachedOutput;
        
    cacheFile.open(fastCacheFilename());
    
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
                        result += lineText;
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
        
        if (result.length())
            result = string(BOLDBLUE) + "Profile        Most Recent Backup                 Finish@   Duration  Size (Total)     Uniq (T)   Saved  Age Range" + RESET + "\n" + result;
        
        cachedOutput = result;
    }
    
    return result;
}


void FastCache::append(FASTCACHETYPE data) {
    cachedData.insert(cachedData.end(), data);
}


void FastCache::set(vector<FASTCACHETYPE> &data) {
    cachedData = data;
    commit();
}


void FastCache::commit() {
    ofstream cacheFile;
    string tempFilename = fastCacheFilename() + ".tmp." + to_string(getpid());

    cacheFile.open(tempFilename);
    
    if (cacheFile.is_open()) {
        for (auto &[lineText, firstTime, lastTime]: cachedData) {
            cacheFile << lineText << endl;
            cacheFile << firstTime << endl;
            cacheFile << lastTime << endl;
        }
    
        cacheFile.close();
        
        unlink(fastCacheFilename().c_str());
        if (rename(tempFilename.c_str(), fastCacheFilename().c_str()))
            log("error: unable to write to " + fastCacheFilename());
    }
}
