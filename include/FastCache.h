
#ifndef FASTCACHE_H
#define FASTCACHE_H

#include <string>
#include <tuple>
#include <vector>

using namespace std;

/* FastCache
 *
 * FastCache is an uncreative name just to distinguish from the existing caches of
 * BackupCache and FaubCache.  FastCache simply persists the text output of the
 * summary status command (i.e. "-0") to a file, along with the mtimes of the first
 * and last backup for each profile.  It facilitates printing the summary stats
 * near instantaneously, only having to calculate the current age of those two
 * mtimes.  The full -0 stats are quite fast as well, but with large backup sets do
 * require a stat() call on every file.  So this is faster.
 *
 * FastCache is updated every time linking, pruning or backing up is performed, as
 * well as any time a -0 stat output is requested.
 */


typedef  tuple<string, time_t, time_t> FASTCACHETYPE;

class FastCache {
    vector<FASTCACHETYPE> cachedData;
    vector<string> cachedFiles;
    string cachedOutput;
    
    bool verifyFileMtimes();
    
public:
    // add a line of -0 status output
    void appendStatus(FASTCACHETYPE);
    
    // add a directory to watch for mtime changes
    void appendFile(string filename);

    // write to disk
    void commit();
    
    // return cache from disk
    string get();
    
    // remove the cache so the next run regenerates it from scratch
    void invalidate();
};


#endif
