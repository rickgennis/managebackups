
#ifndef BASECACHE_H
#define BASECACHE_H

class BaseCache {
    virtual void saveCache() = 0;
    virtual void restoreCache() = 0;

    public:
        BaseCache();
};

#endif

