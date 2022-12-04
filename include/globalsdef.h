
#ifndef GLOBALSDEF_H
#define GLOBALSDEF_H

#define VERSION "1.2.7"

#include "cxxopts.hpp"
#include "colors.h"

#define CONF_DIR "/etc/managebackups"
#define CACHE_DIR "/var/managebackups/caches"
#define TMP_OUTPUT_DIR "/tmp/managebackups_output"


#define DFMT(x) cout << BOLDGREEN << __FUNCTION__ << ": " << RESET << GREEN << x << RESET << endl
#define NOTQUIET (!(GLOBALS.cli.count(CLI_QUIET) || GLOBALS.cli.count(CLI_CRONS) || GLOBALS.cli.count(CLI_CRONP)))
#define ANIMATE (!GLOBALS.cli.count(CLI_ZERO))
#define SCREENERR(x) cerr << RED << x << RESET << endl;

#define SECS_PER_DAY (60*60*24)
#define DATE_REGEX "-(20\\d{2})[-.]*(\\d{2})[-.]*(\\d{2})[-.]"

/* CLI_ and RE_
 * The CLI_ constants are commandline switches while the RE_ are regex patterns
 * that match lines of config files.  It can be a little confusing because some directives
 * can be used in both (--days & days:). */
 
// define commandline options
#define CLI_PROFILE "profile"
#define CLI_DIR "directory"
#define CLI_FILE "file"
#define CLI_COMMAND "command"
#define CLI_DAYS "days"
#define CLI_WEEKS "weeks"
#define CLI_MONTHS "months"
#define CLI_YEARS "years"
#define CLI_FS_BACKUPS "fs_backups"
#define CLI_FS_DAYS "fs_days"
#define CLI_FS_FP "fp"
#define CLI_SCPTO "scp"
#define CLI_SFTPTO "sftp"
#define CLI_NOTIFY "notify"
#define CLI_NOS "nos"
#define CLI_SAVE "save"
#define CLI_NOCOLOR "nocolor"
#define CLI_STATS1 "1"
#define CLI_STATS2 "0"
#define CLI_MAXLINKS "maxlinks"
#define CLI_PRUNE "prune"
#define CLI_NOPRUNE "noprune"
#define CLI_TEST "test"
#define CLI_QUIET "quiet"
#define CLI_DEFAULTS "defaults"
#define CLI_TIME "time"
#define CLI_NOBACKUP "nobackup"
#define CLI_MINSIZE "minsize"
#define CLI_MINSPACE "minspace"
#define CLI_MINSFTPSPACE "minsftpspace"
#define CLI_INSTALL "install"
#define CLI_INSTALLMAN "installman"
#define CLI_HELP "help"
#define CLI_CONFDIR "confdir"
#define CLI_CACHEDIR "cachedir"
#define CLI_LOGDIR "logdir"
#define CLI_DOW "dow"
#define CLI_USER "user"
#define CLI_VERSION "version"
#define CLI_MODE "mode"
#define CLI_ALLSEQ "all"
#define CLI_ALLPAR "All"
#define CLI_NICE "nice"
#define CLI_ZERO "zero"
#define CLI_LOCK "lock"
#define CLI_CRONS "cron"
#define CLI_CRONP "Cron"
#define CLI_RECREATE "recreate"
#define CLI_TRIPWIRE "tripwire"
#define CLI_NOTIFYEVERY "notifyevery"
#define CLI_MAILFROM "from"
#define CLI_LEAVEOUTPUT "leaveoutput"
#define CLI_FAUB "faub"
#define CLI_FAUBCLIENT "faubclient"

// conf file regexes
#define CAPTURE_VALUE string("((?:\\s|=|:)+)(.*?)\\s*?")
#define RE_COMMENT "((?:\\s*#).*)*$"
#define RE_BLANK "^((?:\\s*#).*)*$"
#define RE_PROFILE "(profile)"
#define RE_DIR "(dir|directory)"
#define RE_FILE "(file|filename)"
#define RE_CMD "(command|cmd)"
#define RE_SCP "(scp|scp_to|scpto)"
#define RE_SFTP "(sftp|sftp_to|sftpto)"
#define RE_DAYS "(daily|dailies|days)"
#define RE_WEEKS "(weekly|weeklies|weeks)"
#define RE_MONTHS "(monthly|monthlies|months)"
#define RE_YEARS "(yearly|yearlies|years)"
#define RE_FSBACKUPS "(fs_backups|fb|failsafe_backups)"
#define RE_FSDAYS "(fs_days|fd|failsafe_days)"
#define RE_FS_FP "(fp)"
#define RE_NOTIFY "(notify)"
#define RE_NOS "(nos)"
#define RE_PRUNE "(prune)"
#define RE_MAXLINKS "(maxlinks)"
#define RE_TIME "(time)"
#define RE_MINSIZE "(minsize)"
#define RE_MINSPACE "(minspace)"
#define RE_MINSFTPSPACE "(minsftpspace)"
#define RE_DOW "(dow)"
#define RE_MODE "(mode)"
#define RE_NICE "(nice)"
#define RE_TRIPWIRE "(tw|tripwire)"
#define RE_NOTIFYEVERY "(notifyevery|every)"
#define RE_MAILFROM "(mailfrom|from)"
#define RE_LEAVEOUTPUT "(leaveoutput)"
#define RE_FAUB "(faub|faubstyle)"

#define INTERP_FULLDIR "{fulldir}"
#define INTERP_SUBDIR "{subdir}"
#define INTERP_FILE "{filename}"

#define MILLION 1000000

using namespace std;

enum helpType { hDefaults, hOptions, hExamples, hSyntax };

struct global_vars {
    int sessionId;
    unsigned int debugSelector;
    time_t startupTime;
    unsigned long statsCount;
    unsigned long md5Count;
    int pid;
    cxxopts::ParseResult cli;
    bool color;
    bool stats;
    string logDir;
    string confDir;
    string cacheDir;
    bool saveErrorSeen;
    string interruptFilename;
    string interruptLock;
    int pipes[2][2];
};

#endif

