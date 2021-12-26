#include "BackupEntry.h"
#include "BackupCache.h"
#include "BackupConfig.h"
#include "ConfigManager.h"
#include "cxxopts.hpp"

#include "syslog.h"
#include "unistd.h"
#include <sys/stat.h>
#include <pcre++.h>
#include <iostream>
#include <filesystem>
#include <dirent.h>
#include <time.h>
#include "globalsdef.h"
#include "util.h"


using namespace pcrepp;

struct global_vars GLOBALS;


void parseDirToCache(string directory, string fnamePattern, BackupCache* cache) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    Pcre regEx(fnamePattern);

    if ((c_dir = opendir(directory.c_str())) != NULL ) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                continue;

            string fullFilename = addSlash(directory) + string(c_dirEntry->d_name);

            ++GLOBALS.statsCount;
            struct stat statData;
            if (!stat(fullFilename.c_str(), &statData)) {

                // recurse into subdirectories
                if ((statData.st_mode & S_IFMT) == S_IFDIR) {
                    parseDirToCache(fullFilename, fnamePattern, cache);
                }
                else {
                    // filter for filename if specified
                    if (fnamePattern.length() && !regEx.search(string(c_dirEntry->d_name))) {
                        DEBUG(2) && cout << "skipping due to name mismatch: " << fullFilename << endl;
                        continue;
                    }

                    // if the cache has an existing md5 and the cache's mtime and size match
                    // what we just read from disk, consider the cache valid.  only update
                    // the inode & age info then bail out.
                    BackupEntry *pCacheEntry;
                    if ((pCacheEntry = cache->getByFilename(fullFilename)) != NULL) {
                        if (pCacheEntry->md5.length() && 
                           pCacheEntry->mtime &&
                           pCacheEntry->mtime == statData.st_mtime &&
                           pCacheEntry->size == statData.st_size) {
                            pCacheEntry->links = statData.st_nlink;
                            pCacheEntry->inode = statData.st_ino;

                            cache->addOrUpdate(*pCacheEntry->updateAges(GLOBALS.startupTime));
                            continue;
                        }
                    }

                    // otherwise let's update the cache with everything we just read and
                    // then calculate a new md5
                    BackupEntry cacheEntry;
                    cacheEntry.filename = fullFilename;
                    cacheEntry.links = statData.st_nlink;
                    cacheEntry.mtime = statData.st_mtime;
                    cacheEntry.inode = statData.st_ino;
                    cacheEntry.size = statData.st_size;
                    cacheEntry.updateAges(GLOBALS.startupTime);
                    cacheEntry.calculateMD5();
                    ++GLOBALS.md5Count;

                    cache->addOrUpdate(cacheEntry);                    
                }
            }
            else 
                log("unable to stat " + fullFilename);
        }
        closedir(c_dir);
    }
}


void scanConfigToCache(BackupConfig* config, BackupCache* cache) {
    string directory = "";
    string fnamePattern = "";
    if (config->settings[sDirectory].getValue().length())
        directory = config->settings[sDirectory].getValue();

    // if there's a fnamePattern convert it into a wildcard version to match
    // backups with a date/time inserted.  i.e.
    //    myBigBackup.tgz --> myBigBackup*.tgz
    if (config->settings[sBackupFilename].getValue().length()) {
        Pcre regEx("(.*)\\.([^.]+)$");
        
        if (regEx.search(config->settings[sBackupFilename].getValue()) && regEx.matches()) 
            fnamePattern = regEx.get_match(0) + "-20\\d{2}[-.]*\\d{2}[-.]*\\d{2}.*\\." + regEx.get_match(1);
    }
    else 
        fnamePattern = ".*-20\\d{2}[-.]*\\d{2}[-.]*\\d{2}.*";

    parseDirToCache(directory, fnamePattern, cache);
}


void pruneBackups() {

}


int main(int argc, char *argv[]) {
    GLOBALS.debugLevel = 0;
    GLOBALS.statsCount = 0;
    GLOBALS.md5Count = 0;
    GLOBALS.pid = getpid();

    time(&GLOBALS.startupTime);
    openlog("managebackups", LOG_PID | LOG_NDELAY, LOG_LOCAL1);
    cxxopts::Options options("managebackups", "Create and manage backups");

    options.add_options()
        ("i,integer", "Int param", cxxopts::value<int>())
        ("f,file", "File name", cxxopts::value<std::string>())
        ("v,verbose", "Verbose output", cxxopts::value<bool>()->default_value("false"));
    auto cli = options.parse(argc, argv);

    GLOBALS.debugLevel = cli.count("verbose");

    BackupCache cache;
    BackupConfig config;
    ConfigManager configManager;
    //configManager.configs.begin()++->modified = 1;

    BackupEntry* Myentry = new BackupEntry;
    cout << cache.size() << endl << endl;

    //config.filename = "myFatCat.log";
    config.settings[sDirectory].setValue("/Users/rennis/test"); 
    cache.restoreCache("cachedata.1");
    scanConfigToCache(&config, &cache);

    cout << "size: " << cache.size() << "\t" << endl << endl;
    cout << cache.fullDump() << endl;


    cache.saveCache("cachedata.1");
    cout << "stats: " << GLOBALS.statsCount << ", md5s: " << GLOBALS.md5Count << endl;
return 0;

    Myentry->filename = "foo";
    Myentry->md5 = "abcabcabcabc";
    Myentry->links = 9;
    cache.addOrUpdate(*Myentry);
    cout << cache.size() << "\t" << cache.size(Myentry->md5) << endl << endl;

    Myentry->filename = "bar";
    cache.addOrUpdate(*Myentry);
    cout << cache.size() << "\t" << cache.size(Myentry->md5) << endl << endl;

    Myentry->filename = "fish";
    Myentry->links = 15;
    Myentry->size = 305;
    Myentry->md5 = "ccccccccccccc";
    cache.addOrUpdate(*Myentry);
    cout << cache.size() << "\t" << cache.size(Myentry->md5) << endl << endl;
    cout << cache.fullDump() << endl;

    /*Myentry->filename = "foo";
    Myentry->md5 = "newmd5";
    Myentry->links = 4;
    cache.addOrUpdate(*Myentry);
    cout << cache.size() << "\t" << cache.size(Myentry->md5) << endl << endl;
*/
    cout << cache.fullDump() << endl;

    BackupEntry* entry = cache.getByFilename("fish");
    if (entry != NULL) {
        cout << (*entry).md5 << endl;
        (*entry).mtime = 567;
//        cache.addOrUpdate(*entry);
        cout << cache.fullDump() << endl;
    }

    auto md5set = cache.getByMD5("abcabcabcabc");
    for (auto entry_it = md5set.begin(); entry_it != md5set.end(); ++entry_it) {
        cout << "#" << (*entry_it)->filename << endl;
    }
}
