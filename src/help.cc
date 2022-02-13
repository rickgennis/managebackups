  
#include <iostream>
#include <string>
#include <set>
#include "help.h"
#include "BackupConfig.h"
#include "PipeExec.h"
#include "globals.h"
#include "util_generic.h"


using namespace std;

void showHelp(enum helpType kind) {
    switch (kind) {
        case hDefaults: {
            BackupConfig config;
            cout << "Configuration defaults:" << endl;

            char buffer[200];
            for (auto cfg: config.settings) {
                sprintf(buffer, "   %-15s %s", cfg.display_name.c_str(), cfg.value.c_str());
                cout << buffer << endl;
            }
            break;
        }

        case hOptions: {
            string helpText = "managebackups [options]\n\n"
            + string(BOLDBLUE) + "EXECUTE A BACKUP" + string(RESET) + "\n"
            + "   --directory [dir]   Directory to save to and look for backups in\n"
            + "   --cmd [command]     Command to take a backup; the backup result should be written to STDOUT.\n" 
            + "   --file [filename]   The base filename to save the backup to. The date and, optionally, time will automatically be inserted.\n"
            + "   --time              Include the time in the backup filename; also inserts the day into the subdirectory.\n"
            + "   --mode [mode]       chmod newly created backups to this octatl mode (default 0600).\n"
            + "   --minsize [size]    Backups less than this size are considered failures and discarded.\n"
            + "   --scp [dest]        SCP the new backup to the destination. Dest can include user@machine:/dir/dirs.\n"
            + "   --sftp [dest]       SFTP the new backup to the destination. Dest can include SCP details plus SFTP flags (like -P for port).\n"
            + "                       SCP & SFTP also support variable interpolation of these strings which will be sustituted with values\n"
            + "                       relative to the newly created backup: {fulldir}, {subdir}, {filename}.\n"
            + "   --minspace [size]   Minimum local free space required before taking a backup\n"          
            + "   --minsftpspace [sz] Minimum free space required on the remote SFTP server before transfering a backup\n"          
            + "   --notify [contact]  Notify after a backup completes; can be email addresses and/or script names (failures only).\n"
            + "   --nos               Notify on success also.\n"
            + "\n" + string(BOLDBLUE) + "PRUNING\n" + RESET
            + "   --days [x]          Keep x daily backups\n"
            + "   --weeks [x]         Keep x weekly backups\n"
            + "   --months [x]        Keep x monthly backups\n"
            + "   --years [x]         Keep x yearly backups\n"
            + "   --dow [x]           Day of week to save for weeklies (0=Sunday, 1=Monday, etc). defaults to Sunday.\n"
            + "   --fs_backups [b]    FAILSAFE: Require b backups before pruning\n"
            + "   --fs_days [d]       FAILSAFE: within the last d days.\n"
            + "   --fp                FAILSAFE: Paranoid mode; sets --fb=1, --fd=2\n"
            + "\n" + string(BOLDBLUE) + "HARD LINKING\n" + RESET
            + "   --maxlinks [x]      Max number of links to a file (default 20).\n"
            + "   --prune             Enable pruning.\n"
            + "   --noprune           Disable pruning.\n"
            + "\n" + string(BOLDBLUE) + "GENERAL\n" + RESET
            + "   --profile [name]    Use the specified profile for the current run.\n"
            + "   --save              Save all the specified settings to the specified profile.\n"
            + "   --recreate          Delete any existing .conf file for this profile and recreate it in the standard format\n"
            + "   --all               Execute all profiles sequentially\n"
            + "   -0                  Provide a summary of backups.\n"
            + "   -1                  Provide detail of backups.\n"
            + "   --install           Install this binary in /usr/local/bin, update directory perms and create the man page.\n"
            + "   --installman        Only create and install the man page.\n"
            + "   --confdir [dir]     Use dir for the configuration directory (default /etc/managebackups).\n"
            + "   --cachedir [dir]    Use dir for the cache directory (default /var/managebackups/cache).\n"
            + "   --logdir [dir]      Use dir for the log directory (default /var/log).\n"
            + "   --user              Set directories (config, cache and log) to the calling user's home directory (~/managebackups/).\n"
            + "   --nocolor           Disable color output.\n"
            + "   --test              Run in test mode. No changes are persisted to disk except for caches.\n"
            + "   -q                  Quiet mode -- limit output, for use in scripts.\n"
            + "   -v                  Verbose output for debugging (can be specified multiple times)\n"
            + "   --defaults          Display the default settings for all profiles.\n"
            + "   -x, --lock          Lock the current profile for the duraiton of the run so only one copy can run at a time.\n"
            + "\nSee 'man managebackups' for more detail.\n";

            /*
            // find a pager
            string pagerBin = locateBinary("less");
            if (!pagerBin.length()) {
                pagerBin = locateBinary("more");
                GLOBALS.color = false;
            }
            else
                pagerBin += " -R";  // 'less' can do color

            if (pagerBin.length()) {
                PipeExec pager(pagerBin);

                pager.execute("", true);
                pager.writeProc(helpText.c_str(), helpText.length());
                pager.closeWrite();
                wait(NULL);
            }
            else
            */
                cout << helpText;
        }

        break;

        case hSyntax:
        default:
            cout << R"END(managebackups performs backups and/or manages existing backups. Managing consists
of deleting previous backups via a defined retention schedule (keep the configured
number of daily, weekly, monthly, yearly copies) as well as hard linking backups
that have identical content together to save disk space.

    • Use "managebackups --help" for options.

    • See "man managebackups" for full detail. 

    • Use "sudo managebackups --install" to create the man page if it doesn't exist yet.)END" << endl;
        break;
    }
}


string manPathContent() {
return string(R"END(.\" Automatically generated by Pandoc 2.17.1.1
.\"
.\" Define V font for inline verbatim, using C font in formats
.\" that render this, and otherwise B font.
.ie "\f[CB]x\f[]"x" \{\
. ftr V B
. ftr VI BI
. ftr VB B
. ftr VBI BI
.\}
.el \{\
. ftr V CR
. ftr VI CI
. ftr VB CB
. ftr VBI CBI
.\}
.TH "MANAGEBACKUPS" "1" "January 2022" "managebackups 1.2.5" ""
.hy
.SH NAME
.PP
managebackups - Take and manage backups
.SH SYNOPSIS
.PP
\f[B]managebackups\f[R] [\f[I]OPTION\f[R]]
.SH DESCRIPTION
.PP
\f[B]managebackups\f[R] provides three functions that can be run
independently or in combination:
.SS Take Backups
.PP
Given a backup command (tar, cpio, dump, etc) \f[B]managebackups\f[R]
will execute the command, saving the output to a file named with the
current date (and optionally time).
By default the resulting filename will be of the form
\f[I]directory\f[R]/YYYY/MM/\f[I]filename\f[R]-YYYYMMDD.\f[I]ext\f[R].
When time is included the day of month is also added to the directory
structure
(\f[I]directory\f[R]/YYYY/MM/DD/\f[I]filename\f[R]-YYYYMMDD-HH::MM:SS.\f[I]ext\f[R]).
Note: Without time included (\f[B]\[en]time\f[R]) multiple backups on
the same day taken with the same settings will overwrite each other
resulting in a single backup for the day.
With time included each backup is saved separately.
.SS Prune Backups
.PP
\f[B]managebackups\f[R] deletes old backups that have aged out.
The aging critera is configured on a daily, weekly, monthly and yearly
basis.
By default \f[I]managebackups\f[R] will keep 14 dailies, 4 weeklies, 6
monthlies and 2 yearly backups.
.SS Hard Linking
.PP
In setups where all backups are fulls, and therefore many are
potentially identical, \f[I]managebackups\f[R] can save disk space by
hard linking identical copies together.
This is done by default when identical copies are identified.
.SH PROFILES
.PP
Backup profiles are a collection of settings describing a backup set
\[en] its directory to save backups to, the command to take the backups,
how many weekly copies to keep, etc.
Once a profile is associated with a collection of options, all of those
options are invoked when the profile is specified, unless an overriding
option is also given.
.SH OPTIONS
.PP
Options are relative to the three functions of \f[B]managebackups\f[R].
.SS General Options
.TP
\f[B]\[en]help\f[R]
Displays help text.
.TP
\f[B]-v\f[R]
Provide more verbose output (can be specified several times for
debug-level detail).
.TP
\f[B]\[en]install\f[R]
\f[B]managebackups\f[R] needs write access under /var to store caches of
MD5s and under /etc/managebackups to update configs from commandline
parameters.
It can run entirely as root.
But to facilitate a safer setup, it can be configured to run setgid as
group \[lq]daemon\[rq] and the required directories configured to allow
writes from that group.
\f[B]\[en]install\f[R] installs the \f[B]managebackups\f[R] binary in
/usr/local/bin (setgid), creates the config and cache directories
(writable by \[lq]daemon\[rq]) and installs the man page in
/usr/local/share/man/man1.
It\[cq]s designed for a one-time execution as \f[B]sudo managebackups
\[en]install\f[R] after which root access is no longer required.
Alternatively, all files (config, cache, log) can be written under the
calling user\[cq]s home directory via the \f[B]\[en]user\f[R] option.
But for that setup \f[B]\[en]user\f[R] must be specified on every
invocation.
See \f[B]\[en]user\f[R] for more detail.
.TP
\f[B]\[en]installman\f[R]
Only install the man page to /usr/local/share/man/man1.
.TP
\f[B]-p\f[R], \f[B]\[en]profile\f[R] [\f[I]profile\f[R]]
Use \f[I]profile\f[R] for the current run.
.TP
\f[B]\[en]save\f[R]
Save the currently specified settings (everything on the command line)
with the specified profile name.
.TP
\f[B]\[en]recreate\f[R]
Delete any existing .conf file for this profile and recreate it in the
standard format.
Loses any comments or other existing formatting.
.TP
\f[B]-a\f[R], \f[B]\[en]all\f[R]
Execute all profiles sequentially.
Can be specified by itself to prune, link, and execute backups
(whatever\[cq]s configured) for all profiles.
Or can be combined with limiting options like \f[B]\[en]nobackup\f[R],
\f[B]\[en]noprune\f[R].
.TP
\f[B]-A\f[R], \f[B]\[en]All\f[R]
Execute all profiles in parallel.
Can be specified by itself to prune, link, and execute backups
(whatever\[cq]s configured) for all profiles.
Or can be combined with limiting options like \f[B]\[en]nobackup\f[R],
\f[B]\[en]noprune\f[R].
.TP
\f[B]-0\f[R]
Provide a summary of backups.
.TP
\f[B]-1\f[R]
Provide detail of backups.
.TP
\f[B]\[en]test\f[R]
Run in test mode.
No changes are actually made to disk (no backups, pruning or linking).
.TP
\f[B]\[en]defaults\f[R]
Display the default settings for all profiles.
.TP
\f[B]\[en]nocolor\f[R]
Disable color on console output.
.TP
\f[B]-q\f[R], \f[B]\[en]quiet\f[R]
Quiet mode is to minimize output; useful for cron invocations where
important messages will be seen in the log or via \f[B]\[en]notify\f[R].
.TP
\f[B]\[en]confdir\f[R] [\f[I]dir\f[R]]
Use \f[I]dir\f[R] for all configuration files.
Defaults to /etc/managebackups.
.TP
\f[B]\[en]cachedir\f[R] [\f[I]dir\f[R]]
Use \f[I]dir\f[R] for all cache files.
Defaults to /var/managebackups/caches.
.TP
\f[B]\[en]logdir\f[R] [\f[I]dir\f[R]]
Use \f[I]dir\f[R] for all log files.
Defaults to /var/log if writable by the process, otherwise the
user\[cq]s home directory.
.TP
\f[B]-u\f[R], \f[B]\[en]user\f[R]
Set all three directories (config, cache and log) to use the calling
user\[cq]s home directory (\[ti]/managebackups/).
Directory setting precedence from highest to lowest is a specific
commandline directive (like \f[B]\[en]confdir\f[R]), then
\f[B]\[en]user\f[R], and finally environment variables (shown below).
.TP
\f[B]-x\f[R], \f[B]\[en]lock\f[R]
Lock the specified profile (or all profiles if \f[B]-a\f[R] or
\f[B]-A\f[R]) for the duration of this run.
All subsequent attempts to run this profile while the first one is still
running will be skipped.
The profile is automatically unlocked when the first invocation
finishes.
Locks are respected on every run but only taken out when \f[B]-x\f[R] or
\f[B]\[en]lock\f[R] is specified.
i.e.\ a \f[B]-x\f[R] run will successfully lock the profile even for
other invocations that fail to specify \f[B]-x\f[R].
.SS Take Backups Options
.TP
\f[B]\[en]directory\f[R] [\f[I]directory\f[R]]
Store and look for backups in \f[I]directory\f[R].
.TP
\f[B]\[en]file\f[R] [\f[I]filename\f[R]]
Use \f[I]filename\f[R] as the base filename to create for new backups.
The date and optionally time are inserted before the extension, or if no
extension, at the end.
A filename of mybackup.tgz will become mybackup-YYYYMMDD.tgz.
.TP
\f[B]\[en]cmd\f[R], \f[B]\[en]command\f[R] [\f[I]cmd\f[R]]
Use \f[I]cmd\f[R] to perform a backup.
\f[I]cmd\f[R] should be double-quoted and may include as many pipes as
desired.
Have the command send the backed up data to its STDOUT.
For example, \f[B]\[en]cmd\f[R] \[lq]tar -cz /mydata\[rq] or
\f[B]\[en]cmd\f[R] \[lq]/usr/bin/tar -c /opt | /usr/bin/gzip -n\[rq].
.TP
\f[B]\[en]mode\f[R] [\f[I]mode\f[R]]
chmod newly created backups to \f[I]mode\f[R], which is specified in
octal.
Defaults to 0600.
.TP
\f[B]\[en]time\f[R]
Include the time in the filename of the newly created backup.
The day of month will also be included in the subdirectory.
Without time included multiple backups on the same day taken with the
same settings will overwrite each other resulting in a single backup for
the day.
With time included each backup is saved separately.
.TP
\f[B]\[en]notify\f[R] [\f[I]contact1\f[R], \f[I]contact2\f[R], \&...]
Notify after a backup completes.
By default, only failed backups/SFTP/SCP trigger notifications (see
\f[B]\[en]nos\f[R]).
A contact can be an email address or the full path to a script to
execute.
Double-quote the contact string if it contains any spaces.
The NOTIFICATIONS section below has more detail.
.TP
\f[B]\[en]nos\f[R]
Notify on successful backups also.
.TP
\f[B]\[en]scp\f[R] [\f[I]destination\f[R]]
On completion of a successful backup, SCP the newly created backup file
to \f[I]destination\f[R].
\f[I]destination\f[R] can include user\[at] notation and an optional
hardcoded filename.
If filename is omitted the newly created date-based filename is used,
the same as with a standard cp command.
Additionally the strings {fulldir}, {subdir} and {filename} can be used;
they\[cq]ll be automatically replaced with the values relative to the
newly created backup.
.TP
\f[B]\[en]sftp\f[R] [\f[I]destination\f[R]]
On completion of a successful backup, SFTP the newly created backup file
to \f[I]destination\f[R].
\f[I]destination\f[R] can include user\[at] notation, machine name
and/or directory name.
SFTP parameters (such as -P and others) can be included as well.
Additionally the strings {fulldir}, {subdir} and {filename} can be used;
they\[cq]ll be automatically replaced with the values relative to the
newly created backup.
By default, a current year and month subdirectory will be created on the
destination after connecting and then the file is \[lq]put\[rq] into
that directory.
Use a double-slash anywhere in the \f[I]destination\f[R] to disable
creation and use of the YEAR/MONTH subdirectory structure on the
destination server.
For example, \f[B]\[en]sftp\f[R]
\[lq]backupuser\[at]vaultserver://data\[rq].
.TP
\f[B]\[en]minsize\f[R] [\f[I]minsize\f[R]]
Use \f[I]minsize\f[R] as the minimum size of a valid backup.
Backups created by \f[B]\[en]command\f[R] that are less than
\f[I]minsize\f[R] are considered failures and deleted.
\f[I]minsize\f[R] is assumed to be in bytes unless a suffix is specified
(K, M, G, T, P, E, Z, Y).
The default \f[I]minsize\f[R] is 500.
.TP
\f[B]\[en]minspace\f[R] [\f[I]minspace\f[R]]
Require \f[I]minspace\f[R] free space on the local disk (under
\f[B]\[en]directory\f[R]) before beginning a backup.
\f[I]minspace\f[R] is assumed to be in bytes unless a suffix is
specified (K, M, G, T, P, E, Z, Y).
.TP
\f[B]\[en]minsftpspace\f[R] [\f[I]minsftpspace\f[R]]
Require \f[I]minsftpspace\f[R] free space on the remote SFTP server
before SFTPing a file.
\f[I]minsftpspace\f[R] is assumed to be in bytes unless a suffix is
specified (K, M, G, T, P, E, Z, Y).
.TP
\f[B]\[en]nobackup\f[R]
Disable performing backups for this run.
To disable permanently moving forward, remove the \[lq]command\[rq]
directive from the profile\[cq]s config file.
.SS Pruning Options
.TP
\f[B]\[en]prune\f[R]
By default, \f[B]managebackups\f[R] doesn\[cq]t prune.
Pruning can be enabled with this option or via the config file.
To enable pruning moving forward use \f[B]-p\f[R], \f[B]\[en]save\f[R],
and \f[B]-prune\f[R] together.
Then future runs of that profile will include pruning.
.TP
\f[B]\[en]noprune\f[R]
Disable pruning (when previously enabled) for this run.
Like other options, to make this permanent for the profile moving
forward add \f[B]\[en]save\f[R].
.TP
\f[B]-d\f[R], \f[B]\[en]days\f[R], \f[B]\[en]daily\f[R]
Specify the number of daily backups to keep.
Defaults to 14.
.TP
\f[B]-w\f[R], \f[B]\[en]weeks\f[R], \f[B]\[en]weekly\f[R]
Specify the number of weekly backups to keep.
Defaults to 4.
.TP
\f[B]-m\f[R], \f[B]\[en]months\f[R], \f[B]\[en]monthly\f[R]
Specify the number of monthly backups to keep.
Defaults to 6.
.TP
\f[B]-y\f[R], \f[B]\[en]years\f[R], \f[B]\[en]yearly\f[R]
Specify the number of yearly backups to keep.
Defaults to 2.
.SS Linking Options
.TP
\f[B]-l\f[R], \f[B]\[en]maxlinks\f[R] [\f[I]links\f[R]]
Use \f[I]links\f[R] as the maximum number of links for a backup.
For example, if the max is set to 10 and there are 25 identical content
backups on disk, the first 10 all share inodes (i.e.\ there\[cq]s only
one copy of that data on disk for those 10 backups), the next 10 share
another set of inodes, and the final 5 share another set of inodes.
From a disk space and allocation perspective those 25 identical copies
of data are taking up the space of 3 copies, not 25.
In effect, increasing \f[B]\[en]maxlinks\f[R] saves disk space.
But an accidental mis-edit to one of those files could damage more
backups with a higher number.
Set \f[B]\[en]maxlinks\f[R] to 0 or 1 to disable linking.
Defaults to 20.
.SH NOTIFICATIONS
.PP
\f[B]managebackups\f[R] can notify on success or failure of a backup via
two methods: email or script.
Multiple emails and/or scripts can be specified for the same profile.
.SS Email Notifications
.PP
Notifications are sent to all email addresses configured for the current
profile on every failure.
Notifications are only sent on successes if Notify On Success
(\f[B]\[en]nos\f[R]) is also specified.
.SS Script Notifications
.PP
Notification scripts configured for the current profile are only
considered on a state change.
A state change is defined as a backup succeeding or failing when it did
the opposite in its previous run.
On a state change, all notification scripts for the profile will be
executed if the backup failed.
State changes that change to success are only notified if Notify On
Success (\f[B]\[en]nos\f[R]) is also specified.
In effect, this means the script(s) will only be called for the first in
a string of failures or, with \f[B]\[en]nos\f[R], a string of successes.
.PP
Notification scripts are passed a single parameter, which is a message
describing details of the backup event.
.SH FAILSAFE
.PP
\f[B]managebackups\f[R] can use a failsafe check to make sure that if
backups begin failing, it won\[cq]t continue to prune (remove) old/good
ones.
The failsafe check has two components: the number of successful backups
required (B) and the number of days (D) for a contextual timeframe.
To pass the check and allow pruning, there must be at least B valid
backups within the last D days.
These values can be specified individually via \f[B]\[en]fs_backups\f[R]
and \f[B]\[en]fs_days\f[R].
By default the failsafe check is disabled.
.PP
Rather than specifying the two settings individually, there\[cq]s also a
Failsafe Paranoid option (\f[B]\[en]fp\f[R]) which sets backups to 1 and
days to 2.
In other words, there has to be a valid backup within the last two days
before pruning is allowed.
.SH PROFILE CONFIG FILES
.PP
Profile configuration files are managed by \f[B]managebackups\f[R]
though they can be edited by hand if that\[cq]s easier than lengthy
commandline arguments.
Each profile equates to a .conf file under /etc/managebackups.
Commandline arguments are automatically persisted to a configuration
file when both a profile name (\f[B]\[en]profile\f[R]) and
\f[B]-save\f[R] are specified.
Comments (#) are supported.
.SH EXAMPLES
.TP
\f[B]managebackups \[en]profile homedirs \[en]directory /var/backups \[en]file homedirs.tgz \[en]cmd \[lq]tar -cz /home\[rq] \[en]weekly 2 \[en]notify me\[at]zmail.com \[en]prune \[en]save\f[R]
Create a gzipped backup of /home and store it in
/var/backups/YYYY/MM/homedirs-YYYYMMDD.tgz.
Override the weekly retention to 2 while keeping the daily, monthly and
yearly settings at their defaults.
This performs pruning and linking with their default settings and emails
on failure.
Because \f[B]\[en]save\f[R] is include, all of the settings are
associated with the homedirs profile, allowing the same command to be
run again (or cron\[cq]d) simply as \f[B]managebackups -p homedirs\f[R].
.TP
\f[B]managebackups -p mymac \[en]directory /opt/backups \[en]file mymac.tgz \[en]cmd \[lq]tar -cz /Users /var\[rq] \[en]scp archiver\[at]vaultserver:/mydata \[en]time \[en]notify \[lq]me\[at]zmail.com, /usr/local/bin/push_alert_to_phone.sh\[rq] \[en]save\f[R]
Create a gzipped backup of /Users and /var in
/opt/backups/YYYY/MM/DD/mymac-YYYYMMDD-HH:MM:SS.tgz.
Upon success copy the file to the vaultserver\[cq]s /mydata directory.
Upon failure notify me with email and via a script that pushes an alert
to my phone.
.TP
\f[B]managebackups -p mymac \[en]daily 10 \[en]prune \[en]fp\f[R]
Re-run the mymac profile that was saved in the previous example with all
of its options, but override the daily retention quota, effectively
having \f[B]managebackups\f[R] delete dailies that are older than 10
days.
Also include the Failsafe Paranoid check to make certain a recent backup
was taken before removing any older files.
Because \f[B]\[en]save\f[R] was not specified the \f[B]\[en]daily
10\f[R] (and paranoid setting) is only for this run and doesn\[cq]t
become part of the mymac profile moving forward.
.TP
\f[B]managebackups -p mymac -q\f[R]
Re-run the mymac profile with its last saved configuration
(i.e.\ what\[cq]s in example #2, not #3).
Quiet mode disables all screen output except for errors.
.TP
\f[B]managebackups -a -x\f[R]
Execute all currently defined profiles.
If the above three examples had been run previously two profiles
(homedirs & mymac) would have been defined, each with the associated
parameters on their respective \f[B]\[en]save\f[R] runs.
This \f[B]-a\f[R] invocation would run through each of those profiles
sequentially performing the configured pruning, hard linking and
backups.
\f[B]-x\f[R] locks each profile as it runs (including \[lq]all\[rq]) so
that the same profile can\[cq]t be kicked off again until this run has
finished.
.TP
\f[B]managebackups -a \[en]nobackup\f[R]
Execute all currently defined profiles but don\[cq]t perform backups
\[en] only pruning and linking as configured within each profile.
.TP
\f[B]managebackups \[en]directory /opt/backups \[en]file pegasus.tgz \[en]cmd \[lq]ssh pegasus tar -czf - /home\[rq] \[en]scp me\[at]remoteserver:/var/backups/{subdir}/{file} \[en]prune \[en]fp\f[R]
Tar up /home from the pegasus server and store it in
/opt/backups/YYYY/MM/pegasus-YYYYMMDD.tgz.
Prune and link with default settings, though only prune if there\[cq]s a
recent backup (failsafe paranoid setting).
On success SCP the backup to remoteserver.
.TP
\f[B]managebackups \[en]directory /my/backups \[en]prune\f[R]
Prune (via default thresholds) and update links on all backups found in
/my/backups.
Without \f[B]\[en]file\f[R] or \f[B]\[en]command\f[R] no new backup is
performed/taken.
This can be used to manage backups that are performed by another tool
entirely.
Note: There may be profiles defined with different retention thresholds
for a subset of files in /my/backups (i.e.\ files that match the
\f[B]\[en]files\f[R] setting); those retention thresholds would be
ignored for this run because no \f[B]\[en]profile\f[R] is specified.
.TP
\f[B]managebackups -p mymac \[en]recreate \[en]test\f[R]
Recreate the mymac config file using the standard format.
Previously existing comments and formatting is thrown away.
The \f[B]-test\f[R] option skips all primary functions (no backups,
pruning or linking is done) so only the config file is updated.
.TP
\f[B]managebackups -1\f[R]
Show details of all backups taken that are associated with a profile.
.TP
\f[B]managebackups -0\f[R]
Show a one-line summary for each backup profile.
The summary includes detail on the most recent backup as well as the
number of backups, age ranges and total disk space.
.SH ENVIRONMENT VARIABLES
.PP
Environment variables are overriden by \f[B]\[en]user\f[R],
\f[B]\[en]confdir\f[R], \f[B]\[en]cachedir\f[R], and
\f[B]\[en]logdir\f[R].
.TP
\f[B]MB_CONFDIR\f[R]
Directory to use for configuration files.
See also \f[B]\[en]confdir\f[R].
Defaults to /etc/managebackups.
.TP
\f[B]MB_CACHEDIR\f[R]
Directory to use for cache files.
See also \f[B]\[en]cachedir\f[R].
Defaults to /var/managebackups/caches.
.TP
\f[B]MB_LOGDIR\f[R]
Directory to use for logging.
See also \f[B]\[en]logdir\f[R].
Defaults to /var/log if writable by the process, otherwise the
user\[cq]s home directory.
.SH AUTHORS
Rick Ennis.
)END"); }
