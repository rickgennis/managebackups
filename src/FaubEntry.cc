
#include <fstream>
#include <pcre++.h>
#include "globals.h"
#include "FaubEntry.h"
#include "BackupConfig.h"
#include "util_generic.h"

#include "FaubEntry.h"


FaubEntry::FaubEntry(string dir) {
    directory = dir;
    totalSize = totalSaved = finishTime = duration = 0;
    updated = false;
}


FaubEntry::~FaubEntry() {
    if (updated)
        saveStats();

    if (inodes.size())
        saveInodes();
}


string FaubEntry::stats2string() {
    return(to_string(totalSize) + "," + to_string(totalSaved) + "," + to_string(finishTime) + "," + to_string(duration) + ";");
}


void FaubEntry::string2stats(string& data) {
    Pcre regEx("(\\d+),(\\d+),(\\d+),(\\d+);");

    try {
        if (regEx.search(data) && regEx.matches() > 3) {
            totalSize = stoll(regEx.get_match(0));
            totalSaved = stoll(regEx.get_match(1));
            finishTime = stol(regEx.get_match(2));
            duration = stol(regEx.get_match(3));
        }
        else
            cerr << "error: unable to parse cache line:\n" << data << endl;
        
    }
    catch (...) {
        cerr << "error: unable to parse numeric values from cache line:\n" << data << endl;
    }
}


bool FaubEntry::loadStats() {
    ifstream cacheFile;
    string dirhash = MD5string(directory);

    cacheFile.open(cacheFilename(SUFFIX_FAUBSTATS));
    if (cacheFile.is_open()) {
        string data;

        cacheFile >> data;
        string2stats(data);
        cacheFile.close();

        return true;
    }

    return false;
}


void FaubEntry::saveStats() {
    ofstream cacheFile;
    string filename = cacheFilename(SUFFIX_FAUBSTATS);

    cacheFile.open(filename);
    if (cacheFile.is_open()) {
        string data = stats2string();

        cacheFile << data << endl;
        cacheFile.close();
    }
    else {
        string error = "error: unable to create " + filename;
        log(error);
        SCREENERR(error);
    }
}



void FaubEntry::saveInodes() {
    ofstream cacheFile;
    string inodeData;

    cacheFile.open(cacheFilename(SUFFIX_FAUBINODES));
    if (cacheFile.is_open()) {
        int x = 0;
        for (auto &i: inodes) {
            inodeData += to_string(i) + ",";

            if (++x > 200) {
                inodeData.pop_back();
                inodeData += "\n";
                x = 0;
            }
        }

        cacheFile << inodeData << endl;
        cacheFile.close();
    }
}


void FaubEntry::loadInodes() {
    ifstream cacheFile;
    Pcre inodeRE("(\\d+)");
    string match;

    if (inodes.size())
        return;

    cacheFile.open(cacheFilename(SUFFIX_FAUBINODES));
    if (cacheFile.is_open()) {
        string data;

        while (!cacheFile.eof()) {
            cacheFile >> data;

            size_t pos = 0;
            while (pos <= data.length() && inodeRE.search(data, pos)) {
                pos = inodeRE.get_match_end(0);
                match = inodeRE.get_match(0);
                ++pos;

                if (match.length())
                    inodes.insert(stoll(match)); 
            }
        }

        cacheFile.close();
    }
    else {
        set<ino_t> seenInodes;
        // here we only care about dus() updating 'inodes'
        auto [totalSize, totalSaved] = dus(directory, seenInodes, inodes);
    }
}



