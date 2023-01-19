% MANAGEBACKUPS(1) managebackups 1.3.2
% Rick Ennis
% January 2023

# NAME
managebackups - Take and manage backups

# SYNOPSIS
**managebackups** [*OPTION*]

# DESCRIPTION
**managebackups** provides three functions that can be run independently or in combination:

## Take Backups
Backups can be configured in one of two forms:

- *Single file*:
A single file backup is any of the standard Linux backup commands (tar, cpio, dump) that result in a single compressed file.  Given a backup command (tar, etc) **managebackups** will execute the command, saving the output to a file named with the current date (and optionally time).  By default the resulting filename will be of the form *directory*/YYYY/MM/*filename*-YYYYMMDD.*ext*.  When time is included the day of month is also added to the directory structure (*directory*/YYYY/MM/DD/*filename*-YYYYMMDD-HH::MM:SS.*ext*). Note: Without time included (**--time**) multiple backups on the same day taken with the same settings will overwrite each other resulting in a single backup for the day.  With time included each backup is saved separately.

- *Faubackup*:
Faub-style backups, similar to the underlying approach of Apple's Time Machine, backs up an entire directory tree to another location without compression. The initial copy is an exact replica of the source. Subsequent copies are made to new directories but are hard-linked back to files in the the previous backup if the data hasn't changed. In effect, you only use disk space for changes but get the advantage of fully traversable directory trees, which allows interrogation via any standard commandline tool. Each backup creates a new directory of the form *directory*/YYYY/MM/*profile*-YYYYMMDD. With the **--time** option @HH:MM:SS gets appended as well.

## Prune Backups
**managebackups** deletes old backups that have aged out.  The aging critera is configured on a daily, weekly, monthly and yearly basis.  By default *managebackups* will keep 14 dailies, 4 weeklies, 6 monthlies and 2 yearly backups.

## Hard Linking
In setups where all backups are fulls, and therefore many are potentially identical, *managebackups* can save disk space by hard linking identical copies together.  This is done by default when identical copies are identified. 

# PROFILES
Backup profiles are a collection of settings describing a backup set -- its directory to save backups to, the command to take the backups, how many weekly copies to keep, etc.  Once a profile is associated with a collection of options, all of those options are invoked when the profile is specified, unless an overriding option is also given.

# OPTIONS
Options are relative to the three functions of **managebackups**.

## 0. General Options
**--help**
: Displays help text.

**-v**[*options*]
: Provide verbose debugging output. Brilliant (albeit overkill in this context) debugging logic borrowed from Philip Hazel's Exim Mail Transport Agent. **-v** by itself enables the default list of debugging contexts.  Contexts can be added or subtracted by name. For example, **-v+cache** provides the default set plus caching whereas **-v-all+cache** provides only caching. **-v+all** gives everything (**--vv**, two dashes, two v's, is a synonym for all). Longer combinations can be strung together as well (**-v-all+cache+prune+scan**). Note spaces are not supported in the -v string. Valid contexts are:

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


**--install**
: **managebackups** needs write access under /var to store caches of MD5s and under /etc/managebackups to update configs from commandline parameters. It can run entirely as root. But to facilitate a safer setup, it can be configured to run setgid as group "daemon" and the required directories configured to allow writes from that group. **--install** installs the **managebackups** binary in /usr/local/bin (setgid), creates the config and cache directories (writable by "daemon") and installs the man page in /usr/local/share/man/man1. It's designed for a one-time execution as **sudo managebackups --install** after which root access is no longer required. Alternatively, all files (config, cache, log) can be written under the calling user's home directory via the **--user** option.  But for that setup **--user** must be specified on every invocation.  See **--user** for more detail.

**--installsuid**
: Install **managebackups** in /usr/local/bin as SUID root. With this configuration Faub-style backups can set appropriate owners/groups from the remote systems being backed up while still allowing single-file backups to be saved as the executing user (via --uid and --gid parameters). Config & cache directories and the man page are also created.

**--installman**
: Only install the man page to /usr/local/share/man/man1.

**-p**, **--profile** [*profile*]
: Use *profile* for the current run.  

**--save**
: Save the currently specified settings (everything on the command line) with the specified profile name.

**--recreate**
: Delete any existing .conf file for this profile and recreate it in the standard format. Loses any comments or other existing formatting.

**-a**, **--all**
: Execute all profiles sequentially. Can be specified by itself to prune, link, and execute backups (whatever's configured) for all profiles.  Or can be combined with limiting options like **--nobackup**, **--noprune**.

**-A**, **--All**
: Execute all profiles in parallel. Can be specified by itself to prune, link, and execute backups (whatever's configured) for all profiles.  Or can be combined with limiting options like **--nobackup**, **--noprune**.

**-k**, **--cron**
: Sequential cron execution.  Equivalent to "-a -x -q".

**-K**, **--Cron**
: Parallel cron execution.  Equivalent to "-A -x -q".

**-0**
: Provide a summary of backups. **-0** can be specified up to 3 times for different formatting of sizes.

**-1**
: Provide detail of backups.

**--test**
: Run in test mode. No changes are actually made to disk (no backups, pruning or linking).

**--defaults**
: Display the default settings for all profiles.

**--nocolor**
: Disable color on console output.

**-q**, **--quiet**
: Quiet mode is to minimize output; useful for cron invocations where important messages will be seen in the log or via **--notify**.

**--confdir** [*dir*]
: Use *dir* for all configuration files. Defaults to /etc/managebackups.

**--cachedir** [*dir*]
: Use *dir* for all cache files. Defaults to /var/managebackups/caches.

**--logdir** [*dir*]
: Use *dir* for all log files. Defaults to /var/log if writable by the process, otherwise the user's home directory.

**-u**, **--user**
: Set all three directories (config, cache and log) to use the calling user's home directory (~/managebackups/). Directory setting precedence from highest to lowest is a specific commandline directive (like **--confdir**), then **--user**, and finally environment variables (shown below).

**-x**, **--lock**
: Lock the specified profile (or all profiles if **-a** or **-A**) for the duration of this run.  All subsequent attempts to run this profile while the first one is still running will be skipped.  The profile is automatically unlocked when the first invocation finishes. Locks are respected on every run but only taken out when **-x** or **--lock** is specified.  i.e. a **-x** run will successfully lock the profile even for other invocations that fail to specify **-x**.

**--tripwire** [*string*]
: The tripwire setting can be used as a rudimentary guard against ransomware or other encryption attacks. It can't protect your local backups but will both alert you immediately and stop processing (no pruning, linking or backing up) if the tripwire check fails.  The check is defined as a filename (or list of filenames) and their MD5 values. If any of the MD5s change, the check fails and the alert is triggered.  For example, if you're backing up /etc you can create a bogus test file such as /etc/tripdata.txt and then configure **managebackups** with **--tripwire "/etc/tripdata.txt: xxx"** where xxx is the correct MD5 of the file. Multiple entries can be separated with commas ("/etc/foo: xxx, /etc/fish: yyy, /usr/local/foo: zzz"). Only local computer tripwire files are supported at this time.

## 1. Take Backups Options

**--directory** [*directory*]
: Store and look for backups in *directory*.

**--file** [*filename*]
: Use *filename* as the base filename to create for new backups.  The date and optionally time are inserted before the extension, or if no extension, at the end.  A filename of mybackup.tgz will become mybackup-YYYYMMDD.tgz.

**-c**, **--command** [*cmd*]
: Use *cmd* to perform a backup.  *cmd* should be double-quoted and may include as many pipes as desired. Have the command send the backed up data to its STDOUT.  For example, **--cmd** "tar -cz /mydata" or **--cmd** "/usr/bin/tar -c /opt | /usr/bin/gzip -n".

**--mode** [*mode*]
: chmod newly created backups to *mode*, which is specified in octal. Defaults to 0600.

**--uid** [*uid*]
: chown newly created backups to *uid*, which is specified as an integer. Defaults to the effective executing user. Use 0 to specify the real executing user. For root, leave it unset and run as root. Note: This option only impacts single-file backups; with Faub-style backups files are set to the uid/gid of the remote system if possible (i.e. if run as root or suid), otherwise they remain owned by the executing user.

**--gid** [*gid*]
: chgrp newly created backups to *gid*, which is specified as an integer. Defaults to the effective executing user's group. Note: This option only impacts single-file backups; with Faub-style backups files are set to the uid/gid of the remote system if possible (i.e. if run as root or suid), otherwise they remain owned by the executing user.

**--time**
: Include the time in the filename of the newly created backup.  The day of month will also be included in the subdirectory. Without time included multiple backups on the same day taken with the same settings will overwrite each other resulting in a single backup for the day. With time included each backup is saved separately.

**--notify** [*contact1*, *contact2*, ...]
: Notify after a backup completes. By default, only failed backups/SFTP/SCP trigger notifications (see **--nos**). A contact can be an email address or the full path to a script to execute. Double-quote the contact string if it contains any spaces. The NOTIFICATIONS section below has more detail.

**--notifyevery** [*count*]
: For script notifications, in addition to the initial failure, notify every *count* failures as well.  See the NOTIFICATIONS section.

**--nos**
: Notify on successful backups also.

**--mailfrom** [*address*]
: Use *address* as the sending (i.e. "From") address for outgoing notify email. 

**--scp** [*destination*]
: On completion of a successful backup, SCP the newly created backup file to *destination*.  *destination* can include user@ notation and an optional hardcoded filename.  If filename is omitted the newly created date-based filename is used, the same as with a standard cp command. Additionally the strings {fulldir}, {subdir} and {filename} can be used; they'll be automatically replaced with the values relative to the newly created backup.

**--sftp** [*destination*]
: On completion of a successful backup, SFTP the newly created backup file to *destination*. *destination* can include user@ notation, machine name and/or directory name. SFTP parameters (such as -P and others) can be included as well. Additionally the strings {fulldir}, {subdir} and {filename} can be used; they'll be automatically replaced with the values relative to the newly created backup. By default, a current year and month subdirectory will be created on the destination after connecting and then the file is "put" into that directory. Use a double-slash anywhere in the *destination* to disable creation and use of the YEAR/MONTH subdirectory structure on the destination server.  For example, **--sftp** "backupuser@vaultserver://data".

**--minsize** [*minsize*]
: Use *minsize* as the minimum size of a valid backup. Backups created by **--command** that are less than *minsize* are considered failures and deleted. *minsize* is assumed to be in bytes unless a suffix is specified (K, M, G, T, P, E, Z, Y). The default *minsize* is 500.

**--minspace** [*minspace*]
: Require *minspace* free space on the local disk (under **--directory**) before beginning a backup. *minspace* is assumed to be in bytes unless a suffix is specified (K, M, G, T, P, E, Z, Y).

**--minsftpspace** [*minsftpspace*]
: Require *minsftpspace* free space on the remote SFTP server before SFTPing a file. *minsftpspace* is assumed to be in bytes unless a suffix is specified (K, M, G, T, P, E, Z, Y).

**--nobackup**
: Disable performing backups for this run. To disable permanently moving forward, remove the "command" directive from the profile's config file.

**--leaveoutput**
: Leave the output from any commands that are run to create a backup or SFTP one in a file under /tmp/managebackups_output. This can help facilitate diagnosing authentication errors.

## 2. Pruning Options

**--prune**
: By default, **managebackups** doesn't prune. Pruning can be enabled with this option or via the config file. To enable pruning moving forward use **-p**, **--save**, and **-prune** together. Then future runs of that profile will include pruning.

**--noprune**
: Disable pruning (when previously enabled) for this run. Like other options, to make this permanent for the profile moving forward add **--save**.

**-d**, **--days**, **--daily**
: Specify the number of daily backups to keep. Defaults to 14.

**-w**, **--weeks**, **--weekly**
: Specify the number of weekly backups to keep. Defaults to 4.

**-m**, **--months**, **--monthly**
: Specify the number of monthly backups to keep. Defaults to 6.

**-y**, **--years**, **--yearly**
: Specify the number of yearly backups to keep. Defaults to 2.

## 3. Linking Options

**-l**, **--maxlinks** [*links*]
: Use *links* as the maximum number of links for a backup. For example, if the max is set to 10 and there are 25 identical content backups on disk, the first 10 all share inodes (i.e. there's only one copy of that data on disk for those 10 backups), the next 10 share another set of inodes, and the final 5 share another set of inodes.  From a disk space and allocation perspective those 25 identical copies of data are taking up the space of 3 copies, not 25.  In effect, increasing **--maxlinks** saves disk space. But an accidental mis-edit to one of those files could damage more backups with a higher number. Set **--maxlinks** to 0 or 1 to disable linking. Defaults to 20. 

# NOTIFICATIONS
**managebackups** can notify on success or failure of a backup via two methods: email or script. Multiple emails and/or scripts can be specified for the same profile.

## Email Notifications
Notifications are sent to all email addresses configured for the current profile on every failure.  Notifications are only sent on successes if Notify On Success (**--nos**) is also specified.

## Script Notifications
By default notification scripts configured for the current profile are only considered on a state change. A state change is defined as a backup succeeding or failing when it did the opposite in its previous run. On a state change, all notification scripts for the profile will be executed if the backup failed.  State changes that change to success are only notified if Notify On Success (**--nos**) is also specified. In effect, this means the script(s) will only be called for the first in a string of failures or, with **--nos**, a string of successes. When **--notifyevery** is set to a non-zero number (*count*) a string of successive failures will execute the notify script on every *count* failure. i.e. if *count* is 5 and there's a contiuous succession of failures, every 5th one will run the script, in addition to the first failure.

Notification scripts are passed a single parameter, which is a message describing details of the backup event.

# FAILSAFE
**managebackups** can use a failsafe check to make sure that if backups begin failing, it won't continue to prune (remove) old/good ones. The failsafe check has two components: the number of successful backups required (B) and the number of days (D) for a contextual timeframe. To pass the check and allow pruning, there must be at least B valid backups within the last D days. These values can be specified individually via **--fs_backups** and **--fs_days**. By default the failsafe check is disabled.

Rather than specifying the two settings individually, there's also a Failsafe Paranoid option (**--fp**) which sets backups to 1 and days to 2. In other words, there has to be a valid backup within the last two days before pruning is allowed.

# PROFILE CONFIG FILES
Profile configuration files are managed by **managebackups** though they can be edited by hand if that's easier than lengthy commandline arguments. Each profile equates to a .conf file under /etc/managebackups. Commandline arguments are automatically persisted to a configuration file when both a profile name (**--profile**) and **-save** are specified. Comments (#) are supported.

# EXAMPLES
**managebackups --profile homedirs --directory /var/backups --file homedirs.tgz --cmd "tar -cz /home" --weekly 2 --notify me@zmail.com --prune --save**
: Create a gzipped backup of /home and store it in /var/backups/YYYY/MM/homedirs-YYYYMMDD.tgz. Override the weekly retention to 2 while keeping the daily, monthly and yearly settings at their defaults. This performs pruning and linking with their default settings and emails on failure. Because **--save** is include, all of the settings are associated with the homedirs profile, allowing the same command to be run again (or cron'd) simply as **managebackups -p homedirs**.

**managebackups -p mymac --directory /opt/backups --file mymac.tgz --cmd "tar -cz /Users /var" --scp archiver@vaultserver:/mydata --time --notify "me@zmail.com, /usr/local/bin/push_alert_to_phone.sh" --save**
: Create a gzipped backup of /Users and /var in /opt/backups/YYYY/MM/DD/mymac-YYYYMMDD-HH:MM:SS.tgz. Upon success copy the file to the vaultserver's /mydata directory. Upon failure notify me with email and via a script that pushes an alert to my phone.

**managebackups -p mymac --daily 10 --prune --fp**
: Re-run the mymac profile that was saved in the previous example with all of its options, but override the daily retention quota, effectively having **managebackups** delete dailies that are older than 10 days. Also include the Failsafe Paranoid check to make certain a recent backup was taken before removing any older files.  Because **--save** was not specified the **--daily 10** and paranoid setting are only for this run and don't become part of the mymac profile moving forward.

**managebackups -p mymac -q**
: Re-run the mymac profile with its last saved configuration (i.e. what's in example #2, not #3). Quiet mode disables all screen output except for errors.

**managebackups -a -x**
: Execute all currently defined profiles.  If the above examples had been run previously two profiles (homedirs & mymac) would have been defined, each with the associated parameters on their respective **--save** runs.  This **-a** invocation would run through each of those profiles sequentially performing the configured pruning, hard linking and backups. **-x** locks each profile as it runs (including "all") so that the same profile can't be kicked off again until this run has finished.

**managebackups -a --nobackup**
: Execute all currently defined profiles but don't perform backups -- only pruning and linking as configured within each profile.

**managebackups --directory /opt/backups --file pegasus.tgz --cmd "ssh pegasus tar -czf - /home" --scp me@remoteserver:/var/backups/{subdir}/{file} --prune --fp**
: Tar up /home from the pegasus server and store it in /opt/backups/YYYY/MM/pegasus-YYYYMMDD.tgz. Prune and link with default settings, though only prune if there's a recent backup (failsafe paranoid setting). On success SCP the backup to remoteserver.

**managebackups --directory /my/backups --prune**
: Prune (via default thresholds) and update links on all backups found in /my/backups. Without **--file** or **--command** no new backup is performed/taken. This can be used to manage backups that are performed by another tool entirely. Note: There may be profiles defined with different retention thresholds for a subset of files in /my/backups (i.e. files that match the **--file** setting); those retention thresholds would be ignored for this run because no **--profile** is specified.

**managebackups -p mymac --recreate --test**
: Recreate the mymac config file using the standard format. Previously existing comments and formatting is thrown away. The **-test** option skips all primary functions (no backups, pruning or linking is done) so only the config file is updated.

**managebackups -1**
: Show details of all backups taken that are associated with each profile. Additionally **-p** [*profile*] could be specified to limit the output to a single profile.

**managebackups -0**
: Show a one-line summary for each backup profile. The summary includes detail on the most recent backup as well as the number of backups, age ranges and total disk space.

# ENVIRONMENT VARIABLES
Environment variables are overriden by **--user**, **--confdir**, **--cachedir**, and **--logdir**.

**MB_CONFDIR**
: Directory to use for configuration files. See also **--confdir**. Defaults to /etc/managebackups.

**MB_CACHEDIR**
: Directory to use for cache files. See also **--cachedir**. Defaults to /var/managebackups/caches.

**MB_LOGDIR**
: Directory to use for logging. See also **--logdir**. Defaults to /var/log if writable by the process, otherwise the user's home directory. 

# STATS OUTPUT
Example output from **managebackups -0**


    Profile        Most Recent Backup           Finish@   Duration  Size (Total)  Uniq (T)  Saved  Age Range
    desktop        desktop-20220403.tgz         08:46:19  00:00:50  199M (2.3G)   11 (21)     43%  [4 months, 3 days -> 33 minutes, 13 seconds]
    firewall_logs  firewall-logs-20220403.tbz2  08:58:39  00:13:11  268M (3.5G)   16 (16)      0%  [6 months, 4 days -> 20 minutes, 53 seconds]
    firewall_main  firewall-main-20220403.tgz   08:46:14  00:00:45  118M (1.7G)   21 (28)     29%  [11 months, 1 week -> 33 minutes, 18 seconds]
    icloud         icloud-drive-20220403.tbz2   02:51:29  00:26:40  2.3G (15.9G)  6 (7)        0%  [3 months, 2 days -> 6 hours, 28 minutes]
    laptop         laptop-details-20220403.tgz  08:45:39  00:00:10  8M (96M)      12 (16)     21%  [6 months, 4 days -> 33 minutes, 53 seconds]
    TOTALS                                                00:41:36  2.9G (23.4G)  66 (88)      9%  Saved 2.5G from 25.9G

    199G is the size of the most recent backup of the desktop profile.
    2.3G is the disk space used for all desktop profile backups.
    43% is the percentage saved in desktop profile backups due to hard linking.
    11 is the number of unique desktop profile backups.
    21 is the total number of desktop profile backups (meaning there are 10 dupes).

    2.9G is the total used for the most recent backup of all profiles (i.e. one of each).
    23.4G is the total used for all data together (all data managed by managebackups).
    25.9G is the total that would be used if there were no hard linking.
    All size/space numbers are actual used (thanks to hard linking), except for the 25.9G number.


