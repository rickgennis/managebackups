
#ifndef GLOBALS_H
#define GLOBALS_H

#define CONF_DIR "/etc/managebackups"

#define DEBUG(x) (DEBUG_LEVEL >= x)

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
#define RE_FSBACKS "(failsafe_backups)"
#define RE_FSDAYS "(failsafe_days)"
#define RE_NOTIFY "(notify)"


extern const unsigned int DEBUG_LEVEL;

extern time_t g_startupTime;
extern unsigned long g_stats;
extern unsigned long g_md5s;
extern int g_pid;

#endif

