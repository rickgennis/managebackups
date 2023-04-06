
#ifndef GLOBALSDEF_H
#define GLOBALSDEF_H

#define VERSION "1.4.7c"

#include "cxxopts.hpp"
#include "colors.h"
#include <set>

/*
 Adding a commandline option vs adding a backup config setting.
 
 All settings have CLI options but not all CLI options have an equivalent setting.
 
 (A) To add a CLI option:
    (1) add a defined constant for its name #define CLI_xxxx in globalsdef.h
    (2) add the constant along with its type to options.add_options() in managebackups.cc
 
 (B) To add a backup config setting:
    (1) add a defined constant for its regex #define RE_xxxx in globalsdef.h
    (2) add an enum constant to reference it in Setting.h (order matters, at at end of list)
    (3) add a map entry between the defined const and the enum in Setting.cc
    (4) add it to the settings vector with its default in BackupConfig::BackupConfig(bool) in BackupConfig.cc
    (5) do everything under the CLI option list above because you need a matching CLI option to
      override the config setting
 
 Settings are accessed as config.settings[ENUM].value or config.settings[ENUM].length()
 Because CLI options are already mapped to settings (via B3 above) config.settings[] will already
 have handled reading the settings from the config and overriding it if the CLI option was specified.
 i.e. no need to check the CLI option for a setting.
 
 */


#define CONF_DIR "/etc/managebackups"
#define CACHE_DIR "/var/managebackups/caches"
#define TMP_OUTPUT_DIR "/tmp/managebackups_output"
#define GLOBALSTATSFILE "lastrun.stats"

#define DFMT(x) cerr << BOLDGREEN << __FUNCTION__ << ": " << RESET << GREEN << x << RESET << endl
#define DFMTNOENDL(x) cerr << BOLDGREEN << __FUNCTION__ << ": " << RESET << GREEN << x << RESET
#define DFMTNOPREFIX(x) cerr << GREEN << x << RESET << endl

#define NOTQUIET (!(GLOBALS.cli.count(CLI_QUIET) || GLOBALS.cli.count(CLI_CRONS) || GLOBALS.cli.count(CLI_CRONP)))
#define ANIMATE (!GLOBALS.cli.count(CLI_ZERO))
#define SCREENERR(x) cerr << RED << x << RESET << endl;
#define DUP2(x,y) while (dup2(x,y) < 0 && errno == EINTR)

#define SECS_PER_DAY (60*60*24)
#define DATE_REGEX "-(20\\d{2})[-.]*(\\d{2})[-.]*(\\d{2})(?:@(\\d{2}):(\\d{2}):(\\d{2}))*"

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
#define CLI_INSTALLSUID "installsuid"
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
#define CLI_PATHS "path"
#define CLI_USEBLOCKS "blocks"
#define CLI_UID "uid"
#define CLI_GID "gid"
#define CLI_DIFF "diff"
#define CLI_DIFFL "diffl"
#define CLI_FORCE "force"
#define CLI_SCHED "sched"
#define CLI_SCHEDHOUR "schedhour"
#define CLI_SCHEDMIN "schedminute"
#define CLI_SCHEDPATH "schedpath"
#define CLI_CONSOLIDATE "consolidate"
#define CLI_RECALC "recalc"
#define CLI_BLOAT "bloat"
#define CLI_RELOCATE "relocate"
#define CLI_COMPARE "compare"
#define CLI_THRESHOLD "threshold"
#define CLI_COMPFOCUS "compfocus"


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
#define RE_FAUB "(faub)"
#define RE_PATHS "(path)"
#define RE_UID "(uid)"
#define RE_GID "(gid)"
#define RE_CONSOLIDATE "(consolidate)"
#define RE_BLOAT "(bloat)"

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
    bool useBlocks;
    int pipes[2][2];
    set<int> reapedPids;
};

#endif

