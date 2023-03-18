
#include <dirent.h>
#include <fstream>
#include <pcre++.h>
#include "globals.h"
#include <unistd.h>
#include "FaubEntry.h"
#include "BackupConfig.h"
#include "util_generic.h"

#include "FaubEntry.h"


FaubEntry& FaubEntry::operator=(const DiskStats& stats) {
    ds.sizeInBytes = stats.sizeInBytes;
    ds.sizeInBlocks = stats.sizeInBlocks;
    ds.savedInBytes = stats.savedInBytes;
    ds.savedInBlocks = stats.savedInBlocks;
    return *this;
}


FaubEntry::FaubEntry(string dir, string aProfile) {
    directory = dir;
    ds.sizeInBytes = ds.sizeInBlocks = ds.savedInBytes = ds.savedInBlocks = finishTime = duration = modifiedFiles = unchangedFiles = dirs = slinks = 0;
    startDay = startMonth = startYear = mtimeDayAge = dow = 0;
    profile = aProfile;
    updated = false;
    return;
}


FaubEntry::~FaubEntry() {
    if (updated)
        saveStats();

    if (inodes.size())
        saveInodes();
}


string FaubEntry::stats2string() {
    return(to_string(ds.sizeInBytes) + "," + to_string(ds.sizeInBlocks) + "," + to_string(ds.savedInBytes) + "," + to_string(ds.savedInBlocks) + "," + to_string(finishTime) + "," + to_string(duration) + "," + 
            to_string(modifiedFiles) + "," + to_string(unchangedFiles) + "," + to_string(dirs) + "," + to_string(slinks) + ";");
}


void FaubEntry::string2stats(string& data) {
    Pcre regEx("(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+);");

    try {
        if (regEx.search(data) && regEx.matches() > 9) {
            ds.sizeInBytes = stoll(regEx.get_match(0));
            ds.sizeInBlocks = stoll(regEx.get_match(1));
            ds.savedInBytes = stoll(regEx.get_match(2));
            ds.savedInBlocks = stoll(regEx.get_match(3));
            finishTime = stol(regEx.get_match(4));
            duration = stol(regEx.get_match(5));
            modifiedFiles = stoll(regEx.get_match(6));
            unchangedFiles = stoll(regEx.get_match(7));
            dirs = stoll(regEx.get_match(8));
            slinks = stoll(regEx.get_match(9));
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

    cacheFile.open(cacheFilename(SUFFIX_FAUBSTATS));
    if (cacheFile.is_open()) {
        string data;

        cacheFile >> data;  // path in the file; ignore it here
        cacheFile >> data;  // actual stats
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

        cacheFile << directory << ";;" << profile << endl;
        cacheFile << data << endl;
        cacheFile.close();
    }
    else {
        string error = "error: unable to create " + filename + " - " + strerror(errno);
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
            while (pos <= data.length() && inodeRE.search(data, (int)pos)) {
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
        dus(directory, seenInodes, inodes);
    }
}


void FaubEntry::removeEntry() {
    unlink(cacheFilename(SUFFIX_FAUBSTATS).c_str());
    unlink(cacheFilename(SUFFIX_FAUBINODES).c_str());
    unlink(cacheFilename(SUFFIX_FAUBDIFF).c_str());
}


void FaubEntry::updateDiffFiles(set<string> files) {
    ofstream cacheFile;

    if (files.size()) {
        cacheFile.open(cacheFilename(SUFFIX_FAUBDIFF));
        if (cacheFile.is_open()) {
            for (auto &aFile: files)
                cacheFile << aFile << endl;

            cacheFile.close();
        }
        else {
            string error = "error: unable to create " + cacheFilename(SUFFIX_FAUBDIFF) + " - " + strerror(errno);
            log(error);
            SCREENERR(error);
        }
    }
}


void FaubEntry::displayDiffFiles(bool fullPaths) {
    ifstream cacheFile;
    string data;

    cacheFile.open(cacheFilename(SUFFIX_FAUBDIFF));
    if (cacheFile.is_open()) {
        while (1) {
            cacheFile >> data;
             if (cacheFile.eof())
                 break;

            if (fullPaths)
                data = slashConcat(directory, data);
            
            cout << data << endl;
        }

        cacheFile.close();
    }
    else
        cout << "no diff files found." << endl;
}


int FaubEntry::filenameDayAge() {
    return floor((time(NULL) - filename2Mtime(directory)) / SECS_PER_DAY);
}


void FaubEntry::renameDirectoryTo(string newDir, string oldDir) {
    Pcre regex("^(" + oldDir+ ")");
    
    auto origStats = cacheFilename(SUFFIX_FAUBSTATS);
    auto origInodes = cacheFilename(SUFFIX_FAUBINODES);
    auto origDiff = cacheFilename(SUFFIX_FAUBDIFF);
    
    if (regex.search(directory) && regex.matches()) {
        directory.erase(0, regex.get_match(0).length());
        directory = slashConcat(newDir, directory);
    }

    auto newStats = cacheFilename(SUFFIX_FAUBSTATS);
    auto newInodes = cacheFilename(SUFFIX_FAUBINODES);
    auto newDiff = cacheFilename(SUFFIX_FAUBDIFF);

    // need to rename these if they exist but if they don't
    // that's okay too so no need to error out
    rename(origStats.c_str(), newStats.c_str());
    rename(origInodes.c_str(), newInodes.c_str());
    rename(origDiff.c_str(), newDiff.c_str());
    
    // still need to call save because the 'directory' variable is written
    // into the stats file and needs to be updated.
    saveStats();
}
