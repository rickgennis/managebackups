  
#include <iostream>
#include <string>
#include <set>
#include "help.h"
#include "BackupConfig.h"
#include "ipc.h"
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
                snprintf(buffer, sizeof(buffer), "   %-15s %s", cfg.display_name.c_str(), cfg.value.c_str());
                cout << buffer << endl;
            }
            break;
        }

        case hOptions: {
            string helpText = "managebackups [options]\n\n"
            + string(BOLDBLUE) + "EXECUTE A BACKUP" + string(RESET) + "\n"
            + "   --directory [dir]   Directory to save to and look for backups in\n"
            + "   --cmd [command]     Command to take a single-file backup; the backup result should be written to STDOUT.\n" 
            + "   --faub [command]    Command to take a faub-style backup; managebackups will be required on the remote server as well.\n"
            + "   --file [filename]   The base filename to save the backup to. The date and, optionally, time will automatically be inserted.\n"
            + "   --time              Include the time in the backup filename; also inserts the day into the subdirectory.\n"
            + "   --mode [mode]       chmod newly created backups to this octal mode (default 0600).\n"
            + "   --uid [uid]         chown newly created backups to this numeric UID.\n"
            + "   --gid [gid]         chgrp newly created backups to this numeric GID.\n"
            + "   --minsize [size]    Single-file backups less than this size are considered failures and discarded.\n"
            + "   --scp [dest]        SCP the new backup to the destination. Dest can include user@machine:/dir/dirs.\n"
            + "   --sftp [dest]       SFTP the new backup to the destination. Dest can include SCP details plus SFTP flags (like -P for port).\n"
            + "                       SCP & SFTP also support variable interpolation of these strings which will be sustituted with values\n"
            + "                       relative to the newly created backup: {fulldir}, {subdir}, {filename}.\n"
            + "   --minspace [size]   Minimum local free space required before taking a backup\n"          
            + "   --minsftpspace [sz] Minimum free space required on the remote SFTP server before transfering a backup\n"          
            + "   --notify [contact]  Notify after a backup completes; can be email addresses and/or script names (failures only).\n"
            + "   --notifyevery [x]   Notify on every x failure (plus the first one).\n"
            + "   --nos               Notify on success also.\n"
            + "   --mailfrom [addr]   Use addr as the sending/from address for outgoing notify emails.\n"
            + "\n" + string(BOLDBLUE) + "PRUNING\n" + RESET
            + "   --prune             Enable pruning.\n"
            + "   --noprune           Disable pruning.\n"
            + "   --days [x]          Keep x daily backups\n"
            + "   --weeks [x]         Keep x weekly backups\n"
            + "   --months [x]        Keep x monthly backups\n"
            + "   --years [x]         Keep x yearly backups\n"
            + "   --dow [x]           Day of week to save for weeklies (0=Sunday, 1=Monday, etc); defaults to Sunday.\n"
            + "   --fs_backups [b]    FAILSAFE: Require b backups before pruning\n"
            + "   --fs_days [d]       FAILSAFE: within the last d days.\n"
            + "   --fp                FAILSAFE: Paranoid mode; sets --fs_backups 1 --fs_days 2\n"
            + "\n" + string(BOLDBLUE) + "HARD LINKING\n" + RESET
            + "   -l, --maxlinks [x]  Max number of links to a file (default 20).\n"
            + "\n" + string(BOLDBLUE) + "GENERAL\n" + RESET
            + "   --profile [name]    Use the specified profile for the current run.\n"
            + "   --save              Save all the specified settings to the specified profile.\n"
            + "   --recreate          Delete any existing .conf file for this profile and recreate it in the standard format\n\n"
            + "   -a, --all           Execute all profiles sequentially\n"
            + "   -A, --All           Execute all profiles in parallel\n"
            + "   -k, --cron          Execute all profiles sequentially for cron (equivalent to '-a -x -q')\n"
            + "   -K, --Cron          Execute all profiles in parallel for cron (equivalent to '-A -x -q')\n\n"
            + "   -0                  Provide a summary of backups.\n"
            + "   -1                  Provide detail of backups.\n\n"
            + "   --install           Install this binary in /usr/local/bin, update directory perms and create the man page.\n"
            + "   --installman        Only create and install the man page.\n\n"
            + "   --confdir [dir]     Use dir for the configuration directory (default /etc/managebackups).\n"
            + "   --cachedir [dir]    Use dir for the cache directory (default /var/managebackups/cache).\n"
            + "   --logdir [dir]      Use dir for the log directory (default /var/log).\n"
            + "   --user              Set directories (config, cache and log) to the calling user's home directory (~/managebackups/).\n\n"
            + "   --nocolor           Disable color output.\n"
            + "   --test              Run in test mode. No changes are persisted to disk except for caches.\n"
            + "   --leaveoutput       Leave command (backups, SFTP, etc) output in /tmp/managebackups_output.\n"
            + "   -q                  Quiet mode -- limit output, for use in scripts.\n"
            + "   -v[options]         Verbose debugging output. See 'man managebackups' for details.\n"
            + "   --defaults          Display the default settings for all profiles.\n"
            + "   -x, --lock          Lock the current profile for the duration of the run so only one copy can run at a time.\n"
            + "   --tripwire [string] Define tripwire files of the form 'filename: md5, filename: md5'.\n"
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
return string(R"END(.\" Automatically generated by Pandoc 2.18
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
.TH "MANAGEBACKUPS" "1" "January 2023" "managebackups 1.3.2" ""
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
.SS 1. Take Backups
.PP
Backups can be configured in one of two forms:
.IP \[bu] 2
\f[I]Single file\f[R]: A single file backup is any of the standard Linux
backup commands (tar, cpio, dump) that result in a single compressed
file.
Given a backup command (tar, etc) \f[B]managebackups\f[R] will execute
the command, saving the output to a file named with the current date
(and optionally time).
By default the resulting filename will be of the form
\f[I]directory\f[R]/YYYY/MM/\f[I]filename\f[R]-YYYYMMDD.\f[I]ext\f[R].
When time is included the day of month is also added to the directory
structure
(\f[I]directory\f[R]/YYYY/MM/DD/\f[I]filename\f[R]-YYYYMMDD-HH::MM:SS.\f[I]ext\f[R]).
Note: Without time included (\f[B]\[en]time\f[R]) multiple backups on
the same day taken with the same settings will overwrite each other
resulting in a single backup for the day.
With time included each backup is saved separately.
.IP \[bu] 2
\f[I]Faubackup\f[R]: Faub-style backups, similar to the underlying
approach of Apple\[cq]s Time Machine, backs up an entire directory tree
to another location without compression.
The initial copy is an exact replica of the source.
Subsequent copies are made to new directories but are hard-linked back
to files in the the previous backup if the data hasn\[cq]t changed.
In effect, you only use disk space for changes but get the advantage of
fully traversable directory trees, which allows interrogation via any
standard commandline tool.
Each backup creates a new directory of the form
\f[I]directory\f[R]/YYYY/MM/\f[I]profile\f[R]-YYYYMMDD.
With the \f[B]\[en]time\f[R] option \[at]HH:MM:SS gets appended as well.
Note: Faub-style backups require that \f[B]managebackups\f[R] is also
installed on the remote server that\[cq]s being backed up.
.SS 2. Prune Backups
.PP
\f[B]managebackups\f[R] deletes old backups that have aged out.
The retention critera is configured on a daily, weekly, monthly and
yearly basis.
By default \f[I]managebackups\f[R] will keep 14 dailies, 4 weeklies, 6
monthlies and 2 yearly backups.
.SS 3. Hard Linking
.PP
In configurations using a single file backup where all backups are
fulls, and therefore many are potentially identical,
\f[I]managebackups\f[R] can save disk space by hard linking identical
copies together.
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
Options are relative to the three functions of \f[B]managebackups\f[R]
plus general options.
.SS 0. General Options
.TP
\f[B]\[en]help\f[R]
Displays help text.
.TP
\f[B]-v\f[R][\f[I]options\f[R]]
Provide verbose debugging output.
Brilliant (albeit overkill in this context) debugging logic borrowed
from Philip Hazel\[cq]s Exim Mail Transport Agent.
\f[B]-v\f[R] by itself enables the default list of debugging contexts.
Contexts can be added or subtracted by name.
For example, \f[B]-v+cache\f[R] provides the default set plus caching
whereas \f[B]-v-all+cache\f[R] provides only caching.
\f[B]-v+all\f[R] gives everything (\f[B]\[en]vv\f[R], two dashes, two
v\[cq]s, is a synonym for all).
Longer combinations can be strung together as well
(\f[B]-v-all+cache+prune+scan\f[R]).
Note spaces are not supported in the -v string.
Valid contexts are:
.RS
.IP
.nf
\f[C]
* backup (default)
* cache
* config
* exec
* faub
* link (default)
* netproto
* notify
* prune (default)
* scan (default)
* transfer
* tripwire
\f[R]
.fi
.RE
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
\f[B]\[en]installsuid\f[R]
Install \f[B]managebackups\f[R] in /usr/local/bin as SUID root.
With this configuration Faub-style backups can set appropriate
owners/groups from the remote systems being backed up while still
allowing single-file backups to be saved as the executing user (via
\[en]uid and \[en]gid parameters).
Config & cache directories and the man page are also created.
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
\f[B]-k\f[R], \f[B]\[en]cron\f[R]
Sequential cron execution.
Equivalent to \[lq]-a -x -q\[rq].
.TP
\f[B]-K\f[R], \f[B]\[en]Cron\f[R]
Parallel cron execution.
Equivalent to \[lq]-A -x -q\[rq].
.TP
\f[B]-0\f[R]
Provide a summary of backups.
\f[B]-0\f[R] can be specified up to 3 times for different formatting of
sizes.
.TP
\f[B]-1\f[R]
Provide detail of backups.
\f[B]-1\f[R] can be specified up to 3 times for different formatting of
sizes.
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
\f[B]-b\f[R], \f[B]\[en]blocks\f[R]
By default faub backup size values are displayed in bytes (KB, MB, GB,
etc).
Use \f[B]\[en]blocks\f[R] to instead display disk usage in terms of
blocks, like the the `du' command.
This is only relevant for faub-style backups.
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
.TP
\f[B]\[en]tripwire\f[R] [\f[I]string\f[R]]
The tripwire setting can be used as a rudimentary guard against
ransomware or other encryption attacks.
It can\[cq]t protect your local backups but will both alert you
immediately and stop processing (no pruning, linking or backing up) if
the tripwire check fails.
The check is defined as a filename (or list of filenames) and their MD5
values.
If any of the MD5s change, the check fails and the alert is triggered.
For example, if you\[cq]re backing up /etc you can create a bogus test
file such as /etc/tripdata.txt and then configure
\f[B]managebackups\f[R] with \f[B]\[en]tripwire \[lq]/etc/tripdata.txt:
xxx\[rq]\f[R] where xxx is the correct MD5 of the file.
Multiple entries can be separated with commas (\[lq]/etc/foo: xxx,
/etc/fish: yyy, /usr/local/foo: zzz\[rq]).
Only local computer tripwire files are supported at this time.
.TP
\f[B]\[en]diff\f[R] [\f[I]string\f[R]]
With faub-style backups, \f[B]managebackups\f[R] tracks the files that
have changed between each subsequent backup.
The \f[B]\[en]diff\f[R] option, when given the directory name of a
specific backup, will display the changed files between it and the
previous backup.
The specified backup name can be partial.
.SS 1. Take Backups Options
.PP
Backups options are noted as {1F} for single-file applicable, {FB} for
faub-backup applicable, or {both}.
.TP
\f[B]\[en]directory\f[R] [\f[I]directory\f[R]]
{both} Store and look for backups in \f[I]directory\f[R].
.TP
\f[B]\[en]file\f[R] [\f[I]filename\f[R]]
{1F} Use \f[I]filename\f[R] as the base filename to create for new
backups.
The date and optionally time are inserted before the extension, or if no
extension, at the end.
A filename of mybackup.tgz will become mybackup-YYYYMMDD.tgz.
.TP
\f[B]-c\f[R], \f[B]\[en]command\f[R] [\f[I]cmd\f[R]]
{1F} Use \f[I]cmd\f[R] to perform a backup.
\f[I]cmd\f[R] should be double-quoted and may include as many pipes as
desired.
Have the command send the backed up data to its STDOUT.
For example, \f[B]\[en]cmd\f[R] \[lq]tar -cz /mydata\[rq] or
\f[B]\[en]cmd\f[R] \[lq]/usr/bin/tar -c /opt | /usr/bin/gzip -n\[rq].
\f[B]-c\f[R] is replaced with \f[B]\[en]faub\f[R] in a faub-backup
configuration.
.TP
\f[B]\[en]mode\f[R] [\f[I]mode\f[R]]
{1F} chmod newly created backups to \f[I]mode\f[R], which is specified
in octal.
Defaults to 0600.
.TP
\f[B]\[en]uid\f[R] [\f[I]uid\f[R]]
{1F} chown newly created backups to \f[I]uid\f[R], which is specified as
an integer.
Defaults to the effective executing user.
Use 0 to specify the real executing user.
For root, leave it unset and run as root.
Note: This option only impacts single-file backups; with Faub-style
backups files are set to the uid/gid of the remote system if possible
(i.e.\ if run as root or suid), otherwise they remain owned by the
executing user.
.TP
\f[B]\[en]gid\f[R] [\f[I]gid\f[R]]
{1F} chgrp newly created backups to \f[I]gid\f[R], which is specified as
an integer.
Defaults to the effective executing user\[cq]s group.
Note: This option only impacts single-file backups; with Faub-style
backups files are set to the uid/gid of the remote system if possible
(i.e.\ if run as root or suid), otherwise they remain owned by the
executing user.
.TP
\f[B]\[en]time\f[R]
{both} Include the time in the filename of the newly created backup.
The day of month will also be included in the subdirectory.
Without time included multiple backups on the same day taken with the
same settings will overwrite each other resulting in a single backup for
the day.
With time included each backup is saved separately.
.TP
\f[B]\[en]notify\f[R] [\f[I]contact1\f[R],\f[I]contact2\f[R],\&...]
{both} Notify after a backup completes.
By default, only failed backups/SFTP/SCP trigger notifications (see
\f[B]\[en]nos\f[R]).
A contact can be an email address or the full path to a script to
execute.
Double-quote the contact string if it contains any spaces.
The NOTIFICATIONS section below has more detail.
.TP
\f[B]\[en]notifyevery\f[R] [\f[I]count\f[R]]
{both} For script notifications, in addition to the initial failure,
notify every \f[I]count\f[R] failures as well.
See the NOTIFICATIONS section.
.TP
\f[B]\[en]nos\f[R]
{both} Notify on successful backups also.
.TP
\f[B]\[en]mailfrom\f[R] [\f[I]address\f[R]]
{both} Use \f[I]address\f[R] as the sending (i.e.\ \[lq]From\[rq])
address for outgoing notify email.
.TP
\f[B]\[en]scp\f[R] [\f[I]destination\f[R]]
{1F} On completion of a successful backup, SCP the newly created backup
file to \f[I]destination\f[R].
\f[I]destination\f[R] can include user\[at] notation and an optional
hardcoded filename.
If filename is omitted the newly created date-based filename is used,
the same as with a standard cp command.
Additionally the strings {fulldir}, {subdir} and {filename} can be used;
they\[cq]ll be automatically replaced with the values relative to the
newly created backup.
.TP
\f[B]\[en]sftp\f[R] [\f[I]destination\f[R]]
{1F} On completion of a successful backup, SFTP the newly created backup
file to \f[I]destination\f[R].
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
{1F} Use \f[I]minsize\f[R] as the minimum size of a valid backup.
Backups created by \f[B]\[en]command\f[R] that are less than
\f[I]minsize\f[R] are considered failures and deleted.
\f[I]minsize\f[R] is assumed to be in bytes unless a suffix is specified
(K, M, G, T, P, E, Z, Y).
The default \f[I]minsize\f[R] is 500.
.TP
\f[B]\[en]minspace\f[R] [\f[I]minspace\f[R]]
{1F} Require \f[I]minspace\f[R] free space on the local disk (under
\f[B]\[en]directory\f[R]) before beginning a backup.
\f[I]minspace\f[R] is assumed to be in bytes unless a suffix is
specified (K, M, G, T, P, E, Z, Y).
.TP
\f[B]\[en]minsftpspace\f[R] [\f[I]minsftpspace\f[R]]
{1F} Require \f[I]minsftpspace\f[R] free space on the remote SFTP server
before SFTPing a file.
\f[I]minsftpspace\f[R] is assumed to be in bytes unless a suffix is
specified (K, M, G, T, P, E, Z, Y).
.TP
\f[B]\[en]nobackup\f[R]
{both} Disable performing backups for this run.
To disable permanently moving forward, remove the \[lq]command\[rq]
directive from the profile\[cq]s config file.
.TP
\f[B]\[en]leaveoutput\f[R]
{both} Leave the output from any commands that are run to create a
backup or SFTP one in a file under /tmp/managebackups_output.
This can help facilitate diagnosing authentication errors.
.TP
\f[B]-s\f[R], \f[B]\[en]path\f[R] [\f[I]path\f[R]]
{FB-remote} Specifies which directories to backup in a faub-style
backup.
This option is only used on the REMOTE end, i.e.\ the server being
backed up.
See the FAUB-STYLE BACKUPS section below.
.SS 2. Pruning Options
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
.SS 3. Linking Options
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
Defaults to 100.
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
By default notification scripts configured for the current profile are
only considered on a state change.
A state change is defined as a backup succeeding or failing when it did
the opposite in its previous run.
On a state change, all notification scripts for the profile will be
executed if the backup failed.
State changes that change to success are only notified if Notify On
Success (\f[B]\[en]nos\f[R]) is also specified.
In effect, this means the script(s) will only be called for the first in
a string of failures or, with \f[B]\[en]nos\f[R], a string of successes.
When \f[B]\[en]notifyevery\f[R] is set to a non-zero number
(\f[I]count\f[R]) a string of successive failures will execute the
notify script on every \f[I]count\f[R] failure.
i.e.\ if \f[I]count\f[R] is 5 and there\[cq]s a contiuous succession of
failures, every 5th one will run the script, in addition to the first
failure.
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
.SH PERMISSIONS
.PP
Aside from access to read the files being backed up (on a remote server
or locally) \f[B]managebackups\f[R] requires local write access for
multiple tasks:
.IP \[bu] 2
writing to the log file (default /var/log/managebackups.log)
.IP \[bu] 2
writing its cache files (default /var/managebackups/caches)
.IP \[bu] 2
creating the local backup in the configured directory
.IP \[bu] 2
setting owners/groups/perms on faub-style backup files
.PP
The first three of these can be achieved by moving the log/cache/backup
files into a directory that \f[B]managebackups\f[R] has write access to.
Alternatively, \f[B]managebackups\f[R] can be made suid or sgid (see
\f[B]\[en]install\f[R] and \f[B]\[en]installsuid\f[R]).
The fourth permission issue has no workaround.
If you wish to use faub-style backups \f[B]managebackups\f[R] needs to
run as root either via \f[B]\[en]installsuid\f[R], sudo or via the root
user.
Without root access faub-style backups can be created but all files will
have the same owner.
.SH FAUB-STYLE BACKUPS
.PP
Faub-style backups require \f[B]managebackups\f[R] to be installed on
both the backup server and the server being backed up.
In most cases, both invocations will need to be run as root in order to
replicate the file owners/groups/perms from one machine to the other.
On the server side (the machine doing the backing up), the configuration
will require:
.IP \[bu] 2
\f[B]\[en]directory\f[R]
.IP \[bu] 2
\f[B]\[en]faub\f[R]
.PP
\f[I]not\f[R] \f[B]\[en]command\f[R] or \f[B]\[en]file\f[R].
The \f[B]\[en]faub\f[R] parameter is the remote (likely ssh) command
that invokes \f[B]managebackups\f[R] on the machine to be backed up.
The only parameter the remote invocation of \f[B]managebackups\f[R]
requires is \f[B]\[en]path\f[R] to specify the directory to backup.
\f[B]\[en]path\f[R] can be specified multiple times.
For a more secure setup, create a new user on the machine that\[cq]s
being backed up and only allow it to execute \f[B]managebackups\f[R]
with the exact parameters required.
For example, if you make `backupuser' on the remote `dataserver', you
might include something like this in
\[ti]backupuser/.ssh/authorized_keys2 on dataserver:
.IP
.nf
\f[C]
from=\[dq]192.168.0.0/24\[dq],command=\[dq]sudo managebackups --path /usr/local/bin\[dq] ssh-rsa........<user\[aq]s ssh key>
\f[R]
.fi
.PP
to backup /usr/local/bin, assuming your backup server is connecting from
192.168.0.0/24 and you\[cq]ve allowed backupuser to sudo the command
with NOPASSWD on dataserver.
Alternatively, if you\[cq]ve run \f[B]managebackups
\[en]installsuid\f[R] on dataserver the `sudo' can be omitted.
With this setup the \f[B]\[en]faub\f[R] parameter of your
\f[B]managebackups\f[R] profile configuration could be as simple as:
.IP
.nf
\f[C]
--faub \[dq]ssh dataserver\[dq]
\f[R]
.fi
.PP
Without the authorized_keys2 file you would need the options in your
faub config directly:
.IP
.nf
\f[C]
--faub \[dq]ssh dataserver sudo managebackups --path /usr/local/bin\[dq]
\f[R]
.fi
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
.SH STATS OUTPUT
.PP
Example output from \f[B]managebackups -0\f[R]
.IP
.nf
\f[C]
Profile        Most Recent Backup           Finish\[at]   Duration  Size (Total)   Uniq (T)  Saved  Age Range
desktop        desktop-20230128.tgz         06:41:07  00:00:06  229.4M (4.0G)  18 (21)     14%  [7 months, 1 day -> 7 hours, 26 minutes]
firewall_logs  firewall-logs-20230128.tbz2  06:52:00  00:10:58  212.7M (3.3G)  14 (14)      0%  [1 year, 3 weeks -> 7 hours, 15 minutes]
firewall_main  firewall-main-20230128.tgz   06:48:32  00:07:31  4.5G (12.4G)   22 (26)      6%  [1 year, 3 weeks -> 7 hours, 19 minutes]
fw_faub        fw_faub-20230128\[at]13:34:03    13:34:15  00:00:11  1.2G (5.5G)    6           79%  [4 hours, 17 minutes -> 33 minutes, 20 seconds]
icloud         icloud-drive-20230128.tbz2   06:44:52  00:03:50  2.3G (18.3G)   7 (8)        0%  [1 year, 3 weeks -> 7 hours, 22 minutes]
laptop         laptop-details-20230128.tgz  07:03:51  00:00:00  8.0M (103.3M)  21 (25)     14%  [1 year, 3 weeks -> 7 hours, 3 minutes]
TOTALS                                                00:22:36  8.4G (43.6G)   88 (100)    34%  Saved 22.4G from 66.0G

229.4M is the data size of the most recent backup of the desktop profile (full size, as if there were no hard linking).
4.0G is the actual disk space used for all desktop profile backups (with hard linking).
14% is the percentage saved in desktop profile backups due to hard linking.
18 is the number of unique desktop profile backups (only significant for single-file backups; not faub).
21 is the total number of desktop profile backups (meaning there are 3 dupes).
Age Range is the age of the oldest backup to the age of the most recent backup.

8.4G is the total used for the most recent backup of all profiles (i.e. one of each, as if no hard linking).
43.6G is the total used for all data managed by managebackups together (with hard linking).
66.0G is the total that would be used if there were no hard linking.
\f[R]
.fi
.PP
Example output from \f[B]managebackups -1 -p faub\f[R] (faub-style
backup example)
.IP
.nf
\f[C]
January 2023                                          Size    Used    Dirs    SymLks  Mods    Duration  Type  Age
/var/mybackups/2023/01/28/fw_faub-20230128\[at]09:48:29     5.0G    5.0G      1K     815      9K  00:02:03  Day   4 hours, 27 minutes
/var/mybackups/2023/01/28/fw_faub-20230128\[at]09:50:53     5.0G       0      1K     815       0  00:00:05  Day   4 hours, 26 minutes
/var/mybackups/2023/01/28/fw_faub-20230128\[at]09:57:15     5.0G       0      1K     815       0  00:00:05  Day   4 hours, 19 minutes
/var/mybackups/2023/01/28/fw_faub-20230128\[at]09:59:06     5.0G       0      1K     815       0  00:00:05  Day   4 hours, 17 minutes
/var/mybackups/2023/01/28/fw_faub-20230128\[at]10:00:32     5.0G       0      1K     815       0  00:00:05  Day   4 hours, 16 minutes
/var/mybackups/2023/01/28/fw_faub-20230128\[at]13:34:03     1.2G  488.3M      1K     815       3  00:00:11  Day   42 minutes, 45 seconds

Size is the full size of the data in that backup. Used is is the actual disk space used to store it.
Dirs is the number of directories.
SymLks are the number of symlinks made - these equal the number of symlinks on the remote system being backed up.
Mods is the number of modified files in that backup compared to the previous backup.
\f[R]
.fi
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
10\f[R] and paranoid setting are only for this run and don\[cq]t become
part of the mymac profile moving forward.
.TP
\f[B]managebackups -p mymac -q\f[R]
Re-run the mymac profile with its last saved configuration
(i.e.\ what\[cq]s in example #2, not #3).
Quiet mode disables all screen output except for errors.
.TP
\f[B]managebackups -a -x\f[R]
Execute all currently defined profiles.
If the above examples had been run previously two profiles (homedirs &
mymac) would have been defined, each with the associated parameters on
their respective \f[B]\[en]save\f[R] runs.
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
\f[B]\[en]file\f[R] setting); those retention thresholds would be
ignored for this run because no \f[B]\[en]profile\f[R] is specified.
.TP
\f[B]managebackups -p mymac \[en]recreate \[en]test\f[R]
Recreate the mymac config file using the standard format.
Previously existing comments and formatting is thrown away.
The \f[B]-test\f[R] option skips all primary functions (no backups,
pruning or linking is done) so only the config file is updated.
.TP
\f[B]managebackups -p artemis \[en]directory /opt/backups \[en]faub \[lq]ssh artemis managebackups \[en]path /usr/local/bin \[en]path /etc\[rq] \[en]prune \[en]fp\f[R]
Take a new faub-style backup of server artemis\[cq] /etc and
/usr/local/bin directories, saving them locally to /opt/backups.
Prune older copies of this backup that have aged out.
Note: faub-backups assume \f[B]managebackups\f[R] is installed on the
remote (in this case artemis) server.
.TP
\f[B]managebackups -1\f[R]
Show details of all backups taken that are associated with each profile.
Additionally \f[B]-p\f[R] [\f[I]profile\f[R]] could be specified to
limit the output to a single profile.
.TP
\f[B]managebackups -0\f[R]
Show a one-line summary for each backup profile.
The summary includes detail on the most recent backup as well as the
number of backups, age ranges and total disk space.
.SH AUTHORS
Rick Ennis.
)END"); }
