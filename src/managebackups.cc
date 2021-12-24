#include "BackupEntry.h"
#include "BackupCache.h"
#include "BackupConfig.h"
#include "ConfigManager.h"

#include "syslog.h"
#include "unistd.h"
#include <sys/stat.h>
#include <pcre++.h>
#include <iostream>
#include <filesystem>
#include <dirent.h>
#include <time.h>
#include "globals.h"
#include "util.h"


using namespace pcrepp;

const unsigned int DEBUG_LEVEL = 2;

time_t g_startupTime;
unsigned long g_stats = 0;
unsigned long g_md5s = 0;
int g_pid = getpid();


void parseDirToCache(string directory, string fnamePattern, BackupCache* cache) {
    DIR *c_dir;
    struct dirent *c_dirEntry;
    Pcre regEx(fnamePattern);

    if ((c_dir = opendir(directory.c_str())) != NULL ) {
        while ((c_dirEntry = readdir(c_dir)) != NULL) {
            if (!strcmp(c_dirEntry->d_name, ".") || !strcmp(c_dirEntry->d_name, ".."))
                continue;

            string fullFilename = addSlash(directory) + string(c_dirEntry->d_name);

            ++g_stats;
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

                            cache->addOrUpdate(*pCacheEntry->updateAges(g_startupTime));
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
                    cacheEntry.updateAges(g_startupTime);
                    cacheEntry.calculateMD5();
                    ++g_md5s;

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

    if (config->directory.length()) { directory = config->directory; }

    // if there's a fnamePattern convert it into a wildcard version to match
    // backups with a date/time inserted.  i.e.
    //    myBigBackup.tgz --> myBigBackup*.tgz
    if (config->backup_filename.length()) {
        Pcre regEx("(.*)\\.([^.]+)$");
        
        if (regEx.search(config->backup_filename) && regEx.matches()) 
            fnamePattern = regEx.get_match(0) + "-20\\d{2}[-.]*\\d{2}[-.]*\\d{2}.*\\." + regEx.get_match(1);
    }
    else 
        fnamePattern = ".*-20\\d{2}[-.]*\\d{2}[-.]*\\d{2}.*";

    parseDirToCache(directory, fnamePattern, cache);
}


void pruneBackups() {

}


int main() {
    time(&g_startupTime);
    openlog("managebackups", LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    BackupCache cache;
    BackupConfig config;
    ConfigManager configManager;
    configManager.configs.begin()++->modified = 1;

    BackupEntry* Myentry = new BackupEntry;
    cout << cache.size() << endl << endl;

    //config.filename = "myFatCat.log";
    config.directory = "/Users/rennis/test";
    cache.restoreCache("cachedata.1");
    scanConfigToCache(&config, &cache);

    cout << "size: " << cache.size() << "\t" << endl << endl;
    cout << cache.fullDump() << endl;


    cache.saveCache("cachedata.1");
    cout << "stats: " << g_stats << ", md5s: " << g_md5s << endl;
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
