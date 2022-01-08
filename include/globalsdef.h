
#ifndef GLOBALSDEF_H
#define GLOBALSDEF_H

#include "cxxopts.hpp"

#define CONF_DIR "/etc/managebackups"

#define DEBUG(x,y) (GLOBALS.cli.count(CLI_VERBOSE) >= x) && cout << GREEN << __FUNCTION__ << "\t" << y << RESET << endl
#define NOTQUIET (!GLOBALS.cli.count(CLI_QUIET))

#define DATE_REGEX "-(20\\d{2})[-.]*(\\d{2})[-.]*(\\d{2})[-.]"

/* CLI_ and RE_
 * The CLI_ constants are commandline switches while the RE_ are regex patterns
 * that match lines of config files.  It can be a little confusing because some directives
 * can be used in both (--days & days:). */
 
// define commandline options
#define CLI_TITLE "title"
#define CLI_DIR "directory"
#define CLI_FILE "filename"
#define CLI_COMMAND "command"
#define CLI_DAYS "days"
#define CLI_WEEKS "weeks"
#define CLI_MONTHS "months"
#define CLI_YEARS "years"
#define CLI_FS_BACKUPS "fs_backups"
#define CLI_FS_DAYS "fs_days"
#define CLI_FS_FP "fp"
#define CLI_COPYTO "copyto"
#define CLI_SFTPTO "sftpto"
#define CLI_NOTIFY "notify"
#define CLI_SAVE "save"
#define CLI_VERBOSE "verbose"
#define CLI_NOCOLOR "nocolor"
#define CLI_STATS1 "1"
#define CLI_STATS2 "2"
#define CLI_MAXLINKS "maxlinks"
#define CLI_PRUNE "prune"
#define CLI_NOPRUNE "noprune"
#define CLI_TEST "test"
#define CLI_QUIET "quiet"
#define CLI_DEFAULTS "defaults"
#define CLI_TIME "time"
#define CLI_NOBACKUP "nobackup"

// conf file regexes
#define CAPTURE_VALUE string("((?:\\s|=|:)+)(.*?)\\s*?")
#define RE_COMMENT "((?:\\s*#|//).*)*$"
#define RE_BLANK "^((?:\\s*#|//).*)*$"
#define RE_TITLE "(title)"
#define RE_DIR "(dir|directory)"
#define RE_FILE "(file|filename)"
#define RE_CMD "(command|cmd)"
#define RE_CP "(copy|copyto|copy_to|cp)"
#define RE_SFTP "(sftp|sftp_to|sftpto)"
#define RE_DAYS "(daily|dailies|days)"
#define RE_WEEKS "(weekly|weeklies|weeks)"
#define RE_MONTHS "(monthly|monthlies|months)"
#define RE_YEARS "(yearly|yearlies|years)"
#define RE_FSBACKS "(fs_backups|fb|failsafe_backups)"
#define RE_FSDAYS "(fs_days|fd|failsafe_days)"
#define RE_NOTIFY "(notify)"
#define RE_PRUNE "(prune)"
#define RE_MAXLINKS "(maxlinks)"
#define RE_TIME "(time)"

enum helpType { hDefaults, hOptions, hExamples, hSyntax };

struct global_vars {
    unsigned int debugLevel;
    time_t startupTime;
    unsigned long statsCount;
    unsigned long md5Count;
    int pid;
    int minBackupSize;
    cxxopts::ParseResult cli;
    bool color;
};

#endif

