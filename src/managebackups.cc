#include <iostream>
#include <map>
#include <string>
#include <set>
#include "BackupEntry.h"
#include "BackupCache.h"
using namespace std;

int main() {
    BackupCache cache;
    BackupEntry* Myentry = new BackupEntry;
    cout << cache.size() << endl << endl;

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
    Myentry->bytes = 305;
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
