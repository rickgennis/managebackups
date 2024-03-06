% MANAGEBACKUPS(1) managebackups 1.6.9
% Rick Ennis
% March 2023

# NAME
managebackups - Take and manage backups

# SYNOPSIS
**managebackups** [*OPTION*]

# DESCRIPTION
**managebackups** provides three functions that can be run independently or in combination:

## 1. Take Backups
Backups can be configured in one of two forms:

- *Single file*:
A single file backup is any of the standard Linux backup commands (tar, cpio, dump) that result in a single compressed file.  Given a backup command (tar, etc) **managebackups** will execute the command, saving the output to a file named with the current date (and optionally time).  By default the resulting filename will be of the form *directory*/YYYY/MM/*filename*-YYYY-MM-DD.*ext*.  When time is included the day of month is also added to the directory structure (*directory*/YYYY/MM/DD/*filename*-YYYY-MM-DD-HH::MM:SS.*ext*). Note: Without time included (**--time**) multiple backups on the same day taken with the same settings will overwrite each other resulting in a single backup for the day.  With time included each backup is saved separately.

- *Faubackup*:
Faub-style backups, similar to the underlying approach of Apple's Time Machine, backs up an entire directory tree to another location without compression. The initial copy is an exact replica of the source. Subsequent copies are made to new directories but are hard-linked back to files in the the previous backup if the data hasn't changed. In effect, you only use disk space for changes but get the advantage of fully traversable directory trees, which allows interrogation via any standard commandline tool. Each backup creates a new directory of the form *directory*/YYYY/MM/*profile*-YYYY-MM-DD. With the **--time** option @HH:MM:SS gets appended as well.  As an example, determining when /etc/passwd was changed with faub backups can be as simple as using ls:

~~~~~~~~~~~~
    laptop:~% ls -l /var/backups/*/*/*/firewall*/etc/passwd
    -rw-r--r--  56 root  wheel  2206 Dec 29 21:14 /var/backups/2023/03/01/firewall_main-2023-03-01@00:15:17/etc/passwd
    -rw-r--r--  56 root  wheel  2206 Dec 29 21:14 /var/backups/2023/03/01/firewall_main-2023-03-01@19:48:03/etc/passwd
    -rw-r--r--  56 root  wheel  2206 Dec 29 21:14 /var/backups/2023/03/01/firewall_main-2023-03-01@21:14:36/etc/passwd
    -rw-r--r--  56 root  wheel  2206 Dec 29 21:14 /var/backups/2023/04/01/firewall_main-2023-04-01@17:48:19/etc/passwd
    -rw-r--r--  56 root  wheel  2206 Dec 29 21:14 /var/backups/2023/04/02/firewall_main-2023-04-02@00:15:05/etc/passwd
    -rw-r--r--  56 root  wheel  2206 Dec 29 21:14 /var/backups/2023/04/02/firewall_main-2023-04-02@04:20:11/etc/passwd
    -rw-r--r--   3 root  wheel  2245 Apr 02 07:02 /var/backups/2023/04/02/firewall_main-2023-04-02@08:15:13/etc/passwd
    -rw-r--r--   3 root  wheel  2245 Apr 02 07:02 /var/backups/2023/04/02/firewall_main-2023-04-02@15:07:56/etc/passwd
    -rw-r--r--   3 root  wheel  2245 Apr 02 07:02 /var/backups/2023/04/02/firewall_main-2023-04-02@21:10:22/etc/passwd
~~~~~~~~~~~~

## 2. Prune Backups
**managebackups** deletes old backups that have aged out.  The retention critera is configured on a daily, weekly, monthly and yearly basis.  By default **managebackups** will keep 14 daily, 4 weekly, 6 monthly and 2 yearly backups. Additionally, **managebackups** can perform a second level of pruning called consolidation. If elected, multiple backups taken on the same day can be consolidated down to a single per day backup after said backup has reached a specified age. The default is no consolidation.

## 3. Hard Linking
In configurations using a single file backup where all backups are fulls, and therefore many are potentially identical, **managebackups** can save disk space by hard linking identical copies together.  This is done by default when identical copies are identified. In Faub-style backups hard linking is automatically implemented on a per-file basis.

# PROFILES
Backup profiles are a collection of settings describing a backup set -- its directory to save backups to, the command to take the backups, how many weekly copies to keep, etc.  Once a profile is associated with a collection of options, all of those options are invoked when the profile is specified, unless an overriding option is also given on the commandline.  There are three types of profiles:

## Single File Backup Profile
These specify the settings to perform a single-file backup.  The distinct directives to make it single-file are 'command' and 'file'. 'faub' is not applicable.

## Faub-style Backup Profile
These specify the settings to perform a Faub backup.  The distinct directive to make it Faub is 'faub'.  'command' and 'file' are not applicable.

## Faub client Profile
Faub backups require an instance of **managebackups** running in a server capacity and a client capacity.  If you're backing up a remote server that remote server will run the client version.  The client side can be invoked with the **-s** or **--path** directive, a setup which doesn't require a profile (or config file) on the client side.  Alternatively if there are multiple directives (such as 'path', 'include', 'exclude') it may be simpler to create a client side profile that can be invoked with **-p**.  See FAUB-STYLE BACKUPS below. 

# OPTIONS
Options are categorized by the three functions of **managebackups** plus general options.

## 0. General Options
**--help**
: Displays help text.

**--install**
: Install the **managebackups** binary (SGID daemon) to /usr/local/bin and create the man page. This supports single-file backups.  For use as "sudo managebackups --install". See PERMISSIONS below.

**--installsuid**
: Install the **managebackups** binary (SUID root) to /usr/local/bin and create the man page.  This supports both single-file and Faub-style backups.  For use as "sudo managebackups --installsuid". See PERMISSIONS below.

**--installman**
: Only install the man page to /usr/local/share/man/man1.

**-p**, **--profile** [*profile*]
: Use *profile* for the current run.  

**--save**
: Save the currently specified settings (everything on the command line) with the specified profile name.

**--recreate**
: Delete any existing .conf file for this profile and recreate it in the standard format. Loses any comments or other existing formatting.

**-a**, **--all**
: Execute all profiles sequentially. Can be specified by itself to prune, link, and execute backups (whatever's configured) for all profiles.  Or can be combined with limiting options like **--nobackup**, **--noprune**. Note: Profiles containing the 'path' directive are excluded.

**-A**, **--All**
: Execute all profiles in parallel. Can be specified by itself to prune, link, and execute backups (whatever's configured) for all profiles.  Or can be combined with limiting options like **--nobackup**, **--noprune**. Note: Profiles containing the 'path' directive are excluded.

**-k**, **--cron**
: Sequential cron execution.  Equivalent to "-a -x -q".

**-K**, **--Cron**
: Parallel cron execution.  Equivalent to "-A -x -q".

**-g**, **--go**
: Run (backup, prune, link) the default profile. Or can be combined with limiting options like **--nobackup**, **--noprune**. Normally running a profile is achieved by specifying the **--profile** name and **-g** is not required.  **-g** is simply a shortcut for the default profile, if defined.

**-0**
: Provide a summary of backups. **-0** can be specified up to 5 times for different formatting of sizes. It can also be combined with **-p** to limit output to a single profile. Note: Profiles containing the 'path' directive are excluded.

**-1**
: Provide detail of backups. **-1** can be specified up to 5 times for different formatting of sizes. It can also be combined with **-p** to limit output to a single profile. Note: Profiles containing the 'path' directive are excluded.

**--test**
: Run in test mode. No changes are actually made to disk (no backups, pruning or linking).

**--default**
: Tag the current profile as the default one; only useful with **--save**. Not to be confused with **--defaults**. See DEFAULT PROFILE below.

**--defaults**
: Display the default settings for all profiles.

**--nocolor**
: Disable color on console output.

**-q**, **--quiet**
: Quiet mode is to minimize output; useful for cron invocations where important messages will be seen in the log or via **--notify**.

**-b**, **--blocks**
: By default faub backup size values are displayed in bytes (KB, MB, GB, etc). Use **--blocks** to instead display disk usage in terms of 512-byte blocks, like the the 'du' command. This is only relevant for faub-style backups.

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

**-f**, **--force**
: If used when executing a profile **--force** will override any existing lock and force the profile to run (backup, prune, etc). If used with **--relocate**, **--force** will make every attempt to continue a previously failed **--relocate** by handling errors.  Some assumptions are made in this context.  For example, if a backup is to be renamed A to B and A no longer exists but B already does, it's assumed to have previously succeeded and processing continues.  If a symlink needs to be created and is found to already exist, its deleted and recreated automatically. If used with **--diff** or **--Diff** the Full Changes form of the diff will be run instead of the Cached Changes.

**--tripwire** [*string*]
: The tripwire setting can be used as a rudimentary guard against ransomware or other encryption attacks. It can't protect your local backups but will both alert you immediately and stop processing (no pruning, linking or backing up) if the tripwire check fails.  The check is defined as a filename (or list of filenames) and their MD5 values. If any of the MD5s change, the check fails and the alert is triggered.  For example, if you're backing up /etc you can create a bogus test file such as /etc/tripdata.txt and then configure **managebackups** with **--tripwire "/etc/tripdata.txt: xxx"** where xxx is the correct MD5 of the file. Multiple entries can be separated with commas ("/etc/foo: xxx, /etc/fish: yyy, /usr/local/foo: zzz"). Only local computer tripwire files are supported at this time.

**--diff** [*backup*]
: {FB} Display the differences (files that have changed) between backups.  *backup* can be a partial string that matches any unique backup within the profile.  If **--diff** is specified once, its performed on the matching backup and the chronologically previous backup.  If **--diff** is specified twice, the two given backups are diff'd.  **--force** can be used to generate the full version of the diff when **--diff** is only specified once.  Note that if a file exceeds **--maxlinks** and gets assigned a new inode (even though the file content is identical) it will also show up in a **--diff**.  See **--threshold**.  See EXAMINING BACKUPS below for full detail.

**--Diff** [*backup*]
: {FB} Directories & symlinks can't be hardlinked, meaning they would show up on every **--diff** even if the data hasn't changed. To help focus on actual changes **--diff** filters those out.  **--Diff** provides the same functionality, but includes the directories & symlinks.  **--Diff** unfiltering only applies to Full Changes diffs (see EXAMINING BACKUPS).

**--last**
: {FB} Perform a diff (see **--diff**) on the most recent backup in the profile without having to specify that backup.  Can be combined with **--force** and/or **--threshold**.

**--threshold** [*limit*]
: {FB} Specify a threshold to filter **--diff** or **--Diff** listings. *limit* is assumed to be in bytes unless a suffix is specified (K, M, G, T, P, E, Z, Y).  Alternatively *limit* can be a percentage (e.g. 25%).  If the absolute value of the difference between the size of the file in backupA and the size of the file in backupB is greater than or equal to the *limit* then the file is included in the output. A file that exists in only one of the two backups matches any percentage.  Defaults to 0 to include all changes. 

**--relocate** [*newDir*]
: Relocating backups for a profile entails updating the internal caches, updating the profile's configuration and moving all the backups' files in a hardlink-aware way. The **--relocate** option handles all three of these. Use it in conjunction with **-p**.

**--sched** [*hours*]
: Schedule (via LaunchCtl on MacOS or cron on Linux) **managebackups** to run every *hours* hours with the "-K" option.  If *hours* is 0 or 24 it's interpreted as once a day and will default to 00:15 in the morning.  If set to run once a day **--schedhour** can be specified to use a different single hour. **--schedminute** can be used to specify a different minute offset. **--schedpath** can be used to specify an alternative location if **managebackups** isn't installed in /usr/local/bin.

**--schedhour** [*hour*]
: Specify which hour to run at.  See **--sched**.

**--schedminute** [*minute*]
: Specify which minute offset to run at.  See **--sched**.

**--schedpath** [*path*]
: Specify the path to **managebackups** if it isn't installed in /usr/local/bin.  See **--sched**.

**--recalc**
: Recalcuate all disk usage for a profile. Use with -p. This should never be necessary unless you manually modify a backup.

**-v**[*options*]
: Provide verbose debugging output. Brilliant (albeit overkill in this situation) debugging logic borrowed from Philip Hazel's Exim Mail Transport Agent. **-v** by itself enables the default list of debugging contexts.  Contexts can be added or subtracted by name. For example, **-v+cache** provides the default set plus caching whereas **-v-all+cache** provides only caching. **-v+all** gives everything (**--vv**, two dashes, two v's, is a synonym for all). Longer combinations can be strung together as well (**-v-all+cache+prune+scan**). Note spaces are not supported in the -v string. Valid contexts are:

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

**--dow** [*num*]
: Specify which day of week to take weekly backups. 0 = Sunday, 1 = Monday...  Default is 0.

**--archive**
: Set the profile to Archive mode; use with **--save**. In Archive mode the profile is skipped from "all" runs (**-a** & **-A**). It can still be run manually via **--profile**.  Even on a manual run, pruning is disabled for Archive profiles, unless specifically overridden with **--prune**.

## 1. Take Backups Options
Backups options are noted as {1F} for single-file applicable, {FB} for faub-backup applicable, or {both}.

**--directory** [*directory*]
: {both} Store and look for backups in *directory*.

**--file** [*filename*]
: {1F} Use *filename* as the base filename to create for new backups.  The date and optionally time are inserted before the extension, or if no extension, at the end.  A filename of mybackup.tgz will become mybackup-YYYYMMDD.tgz.

**--faub** [*cmd*]
: {FB} Use *cmd* to perform a Faub-style backup. *cmd* should be double-quoted.  Ultimately *cmd* should execute **managebackups** with **--path** or **-s** on the server to be backed up.  If the backup is of the localhost *cmd* can simply be "managebackups -s filesystemToBeBackedup".  If the backup is of a remote host *cmd* needs to execute **managebackups** on that remote server, such as via ssh.  See the FAUB-STYLE BACKUPS section below. 

**-c**, **--command** [*cmd*]
: {1F} Use *cmd* to perform a single-file backup.  *cmd* should be double-quoted and may include as many pipes as desired. Have the command send the backed up data to its STDOUT.  For example, **--cmd** "tar -cz /mydata" or **--cmd** "/usr/bin/tar -c /opt | /usr/bin/gzip -n".  **-c** is replaced with **--faub** in a faub-backup configuration.

**--mode** [*mode*]
: {1F} chmod newly created backups to *mode*, which is specified in octal. Defaults to 0600.

**--uid** [*uid*]
: {1F} chown newly created backups to *uid*, which is specified as an integer. Defaults to the effective executing user. Use 0 to specify the real executing user. For root, leave it unset and run as root. Note: This option only impacts single-file backups; with Faub-style backups files are set to the uid/gid of the remote system if possible (i.e. if run as root or suid), otherwise they remain owned by the executing user.

**--gid** [*gid*]
: {1F} chgrp newly created backups to *gid*, which is specified as an integer. Defaults to the effective executing user's group. Note: This option only impacts single-file backups; with Faub-style backups files are set to the uid/gid of the remote system if possible (i.e. if run as root or suid), otherwise they remain owned by the executing user.

**--time**
: {both} Include the time in the filename of the newly created backup.  The day of month will also be included in the subdirectory. Without time included multiple backups on the same day taken with the same settings will overwrite each other resulting in a single backup for the day. With time included each backup is saved separately.

**--notify** [*contact1*,*contact2*,...]
: {both} Notify after a backup completes. By default, only failed backups/SFTP/SCP trigger notifications (see **--nos**). A contact can be an email address or the full path to a script to execute. Double-quote the contact string if it contains any spaces. The NOTIFICATIONS section below has more detail.

**--notifyevery** [*count*]
: {both} For script notifications, in addition to the initial failure, notify every *count* failures as well.  See the NOTIFICATIONS section.

**--nice** [*nice*]
: {both} Specify a nice value for the backup to run as.  Defaults to 0. See "man nice".

**--nos**
: {both} Notify on successful backups also.

**--bloat** [*size*]
: {both} Notify if a newly taken backup is *size* larger than the average size backup for this profile.  *size* can be a specific number (e.g. "2G", "500K", etc) or a percentage (e.g. "80%").  The average size is calculated from the most recent 10 backups.  For single-file backups, the full size of the backups are being compared.  For faub-style backups, it's the actual disk usage, not the full size.

**--mailfrom** [*address*]
: {both} Use *address* as the sending (i.e. "From") address for outgoing notify email. 

**--scp** [*destination*]
: {1F} On completion of a successful backup, SCP the newly created backup file to *destination*.  *destination* can include user@ notation and an optional hardcoded filename.  If filename is omitted the newly created date-based filename is used, the same as with a standard cp command. Additionally the strings {fulldir}, {subdir} and {filename} can be used; they'll be automatically replaced with the values relative to the newly created backup.

**--sftp** [*destination*]
: {1F} On completion of a successful backup, SFTP the newly created backup file to *destination*. *destination* can include user@ notation, machine name and/or directory name. SFTP parameters (such as -P and others) can be included as well. Additionally the strings {fulldir}, {subdir} and {filename} can be used; they'll be automatically replaced with the values relative to the newly created backup. By default, a current year and month subdirectory will be created on the destination after connecting and then the file is "put" into that directory. Use a double-slash anywhere in the *destination* to disable creation and use of the YEAR/MONTH subdirectory structure on the destination server.  For example, **--sftp** "backupuser@vaultserver://data".

**--minsize** [*minsize*]
: {1F} Use *minsize* as the minimum size of a valid backup. Backups created by **--command** that are less than *minsize* are considered failures and deleted. *minsize* is assumed to be in bytes unless a suffix is specified (K, M, G, T, P, E, Z, Y). The default *minsize* is 500.

**--minspace** [*minspace*]
: {1F} Require *minspace* free space on the local disk (under **--directory**) before beginning a backup. *minspace* is assumed to be in bytes unless a suffix is specified (K, M, G, T, P, E, Z, Y).

**--minsftpspace** [*minsftpspace*]
: {1F} Require *minsftpspace* free space on the remote SFTP server before SFTPing a file. *minsftpspace* is assumed to be in bytes unless a suffix is specified (K, M, G, T, P, E, Z, Y).

**--nobackup**
: {both} Disable performing backups for this run. To disable permanently moving forward, remove the "command" & "faub" directives from the profile's config file.

**--leaveoutput**
: {both} Leave the output from any commands that are executed (create a backup, ssh, SFTP, etc) in a file under /tmp/managebackups_output. This can help facilitate diagnosing authentication or configuration errors.

**--include** [*pattern*]
: {FB} Only backup directory entries that match the specified regex pattern. By default this option only filters files and continues to include subdirectories themselves (files in the subdirectories are filtered). To have it apply to the subdirectories see **--filterdirs**.

**--exclude** [*pattern*]
: {FB} Only backup directory entries that do NOT match the specified regex pattern. By default this option only filters files and continues to include subdirectories themselves (files in the subdirectories are filtered). To have it apply to the subdirectories see **--filterdirs**.

**--filterdirs**
: {FB} Apply **--include** or **--exclude** filtering to subdirectory names.

**-s**, **--path** [*path*]
: {FB-remote} Specifies which directories to backup in a faub-style backup.  This option is only used on the REMOTE end, i.e. the server being backed up. See the FAUB-STYLE BACKUPS section below. Multiple paths can be specified via quoted parameters that are space delimited (--path "/usr /usr/local /root") or multiple directives (--path /usr --path /usr/local). Note: If specified on the commandline and in a selected profile, the commandline paths replace *all* of the ones in the profile for that one.

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

**--consolidate** [*days*]
: Prune backups down to a single backups per day once they're *days* days old. If you take multiple backups of the same profile in the same day (with **--time**), this allows you to keep only a single one after they age to *days* days but haven't reached the full retention policy to be completely deleted.

**--dataonly**
: {FB} Only retain backups with changed data. Backups with no changes (identical copies of the previous backup) are removed on the next run.

**--fs_days** [*days*]
: Failsafe - Require *backups* backups (see **--fs_backups**) within the last *days* days before pruning will begin.  See FAILSAFE below.

**--fs_backups** [*backups*]
: Failsafe - Require *backups* backups within the last *days* days (see **--fs_days**) before pruning will begin.  See FAILSAFE below.

**--fs_limit** [*backups*]
: Failsafe - Limit pruning to a max of *backups* backups per run.  i.e. if *backups* is 4 then no more than 4 backups will be pruned for the current profile per run.  See FAILSAFE below.

**--fp**
: Failsafe paranoid - See FAILSAFE below.

## 3. Linking Options

**-l**, **--maxlinks** [*links*]
: Use *links* as the maximum number of links for a backup. For example, if the max is set to 10 and there are 25 identical content backups on disk, the first 10 all share inodes (i.e. there's only one copy of that data on disk for those 10 backups), the next 10 share another set of inodes, and the final 5 share another set of inodes.  From a disk space and allocation perspective those 25 identical copies of data are taking up the space of 3 copies, not 25.  In effect, increasing **--maxlinks** saves disk space. But an accidental mis-edit to one of those files could damage more backups with a higher number. Set **--maxlinks** to 0 or 1 to disable linking. Defaults to 200.

# NOTIFICATIONS
**managebackups** can notify on success or failure of a backup via two methods: email or script. Multiple emails and/or scripts can be specified for the same profile.

## Email Notifications
Notifications are sent to all email addresses configured for the current profile on every failure.  Notifications are only sent on successes if Notify On Success (**--nos**) is also specified.

## Script Notifications
By default notification scripts configured for the current profile are only considered on a state change. A state change is defined as a backup succeeding or failing when it did the opposite in its previous run. On a state change, all notification scripts for the profile will be executed if the backup failed.  State changes that change to success are only notified if Notify On Success (**--nos**) is also specified. In effect, this means the script(s) will only be called for the first in a string of failures or, with **--nos**, a string of successes. When **--notifyevery** is set to a non-zero number (*count*) a string of successive failures will execute the notify script on every *count* failure. i.e. if *count* is 5 and there's a contiuous succession of failures, every 5th one will run the script, in addition to the first failure.

Notification scripts are passed a single parameter, which is a message describing details of the backup event.

# FAILSAFE
**managebackups** can use a failsafe check to make sure that if backups begin failing, it won't continue to prune (remove) old/good ones. The failsafe check has two components: the number of successful backups required (B) and the number of days (D) for a contextual timeframe. To pass the check and allow pruning, there must be at least B valid backups within the last D days. These values can be specified individually via **--fs_backups** and **--fs_days**. By default the failsafe check is disabled.

The failsave also has a limit option, **--fs_limit**.  When a limit is given, it's interpreted as the maximum number of backups to prune for this profile in a single run.

Rather than specifying the settings individually, there's also a Failsafe Paranoid option (**--fp**) which sets backups to 1, days to 2, and limit to 2. In other words, there has to be a valid backup within the last two days before pruning is allowed; and if allowed, a max of 2 backups will be pruned per run.

# PROFILE CONFIG FILES
Profile configuration files are managed by **managebackups** though they can be edited by hand if that's easier than lengthy commandline arguments. Each profile equates to a .conf file under /etc/managebackups. Commandline arguments are automatically persisted to a configuration file when both a profile name (**--profile**) and **-save** are specified. Comments (#) are supported.

# FAUB-STYLE BACKUPS
Faub-style backups require **managebackups** to be installed on both the backup server and the server being backed up.  In most cases, both invocations will need to be run as root in order to replicate the file owners/groups/perms from one machine to the other.  On the server side (the machine doing the backing up) the configuration will require:

- **--directory**
- **--faub**

*not* **--command** or **--file**.  The **--faub** parameter is the remote (likely ssh) command that invokes **managebackups** on the machine to be backed up.  The only parameter the remote invocation of **managebackups** requires is **--path** to specify the directory to backup.  **--path** can be specified multiple times. For a more secure setup, create a new user on the machine that's being backed up and only allow it to execute **managebackups** with the exact parameters required.  For example, if you make 'backupuser' on the remote 'dataserver', you might include something like this in ~backupuser/.ssh/authorized_keys2 on dataserver:

    from="192.168.0.0/24",command="sudo managebackups --path /usr/local/bin" ssh-rsa........<user's ssh key>

to backup /usr/local/bin, assuming your backup server is connecting from 192.168.0.0/24 and you've allowed backupuser to sudo the command with NOPASSWD on dataserver. Alternatively, if you've run **managebackups --installsuid** on dataserver the 'sudo' can be omitted. With this setup the **--faub** parameter of your **managebackups** profile configuration could be as simple as:

    --faub "ssh dataserver"

Without the authorized_keys2 file you would need the options in your faub config directly:

    --faub "ssh dataserver sudo managebackups --path /usr/local/bin"
    
Another simplification would be to create a profile (for example, "dataclient") on the dataserver that includes the 'path' directive and any others desired options (include, exclude, filterdirs, etc).  Then the --faub command on the server could become:

    --faub "ssh dataserver sudo managebackups -p dataclient"

Complications with configuration of faub, particularly if ssh is involved, are much easier to debug given the output of the various subcommands.  See **--leaveoutput**.

# EXAMINING BACKUPS
**managebackups** provides two methods to inspect the difference between individual Faub-style backups within a profile.  

## Type: Cached Changes
Cached changes are saved when a backup is taken. They provide minimal detail (only the filename), are instantaneous to retrieve, and relative to the chronologically previous backup at the time it was taken.  That means if the previous backup is deleted the cached output --which doesn't change-- will be showing the diffs to a now non-existent backup, *not* to what now appears to be the previous backup.  Cached changes only list files that were added or modified, not deleted.

## Type: Full Changes
Full changes provide all detail: files that were added, modified or deleted, how much the size of the file changed up or down and whether it's a link, directory or regular file.  Because **managebackups** has to walk the filesystem to determine the differences, this approach is slower.  A full changes diff can be requested of any two backups within the profile, unlike cached changes, which will always compare to the immediately previous one.

## General
The **--diff** [*backup*] command, if only specified once, will default to Cached Changes and show the changes between the specified *backup* and the immediately previous one (at the time it was taken).  Use **--force** to generate the Full Changes diff instead.  When **--diff** [*backup*] is specified twice (i.e. which two backups to compare), it always does a Full Changes diff.  Note: Because symlinks and directories can't be hardlinked they always show up as changes and are automatically filtered out of the **--diff** output.  To include them use **--Diff** instead.  **--threshold** can be used to further filter the output to only show files that have changed by a certain size or more.  **--last** can be used as a shortcut to run **--diff** on the most recent backup without having to specify the backup itself.

# PERMISSIONS
Aside from access to read the files being backed up (on a remote server or locally) **managebackups** requires local write access for multiple tasks:

- writing to the log file (default /var/log/managebackups.log)
- writing its cache files (default /var/managebackups/caches)
- creating the local backup in the configured directory
- setting owners/groups/perms on faub-style backup files

The first three of these can be achieved by moving the log/cache/backup files into a directory that **managebackups** has write access to. Alternatively, **managebackups** can be made suid or sgid (see **--install** and **--installsuid**). The fourth permission issue has no workaround. If you wish to use faub-style backups **managebackups** needs to run as root either via **--installsuid**, sudo or via the root user.  Without root access faub-style backups can be created but all files will have the same owner.

# ENVIRONMENT VARIABLES
Environment variables are overriden by **--user**, **--confdir**, **--cachedir**, and **--logdir**.

**MB_CONFDIR**
: Directory to use for configuration files. See also **--confdir**. Defaults to /etc/managebackups.

**MB_CACHEDIR**
: Directory to use for cache files. See also **--cachedir**. Defaults to /var/managebackups/caches.

**MB_LOGDIR**
: Directory to use for logging. See also **--logdir**. Defaults to /var/log if writable by the process, otherwise the user's home directory. 

# DEFAULT PROFILE
On a system with multiple profiles where one is used most of the time that profile can be made the default.  A default profile is automatically selected, without the need to specify it via **-p** for the following options:

        --diff
        --Diff
        --last
        --recalc
        --relocate
        --go

Even with a default specified **-p** can always be used to override.

When only one profile is defined it is automatically the default.

# STATS OUTPUT
Example output from **managebackups -0**

    Profile        Most Recent Backup           Finish@   Duration  Size (Total)   Uniq (T)  Saved  Age Range
    desktop        desktop-20230128.tgz         06:41:07  00:00:06  229.4M (4.0G)  18 (21)     14%  [7 months, 1 day -> 7 hours, 26 minutes]
    firewall_logs  firewall-logs-20230128.tbz2  06:52:00  00:10:58  212.7M (3.3G)  14 (14)      0%  [1 year, 3 weeks -> 7 hours, 15 minutes]
    firewall_main  firewall-main-20230128.tgz   06:48:32  00:07:31  4.5G (12.4G)   22 (26)      6%  [1 year, 3 weeks -> 7 hours, 19 minutes]
    fw_faub        fw_faub-20230128@13:34:03    13:34:15  00:00:11  1.2G (5.5G)    6           79%  [4 hours, 17 minutes -> 33 minutes, 20 seconds]
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

Example output from **managebackups -1 -p faub** (faub-style backup example)

    January 2023                                          Size    Used    Dirs    SymLks  Mods    Duration  Type  Age
    /var/mybackups/2023/01/28/fw_faub-20230128@09:48:29     5.0G    5.0G      1K     815      9K  00:02:03  Day   4 hours, 27 minutes
    /var/mybackups/2023/01/28/fw_faub-20230128@09:50:53     5.0G       0      1K     815       0  00:00:05  Day   4 hours, 26 minutes
    /var/mybackups/2023/01/28/fw_faub-20230128@09:57:15     5.0G       0      1K     815       0  00:00:05  Day   4 hours, 19 minutes
    /var/mybackups/2023/01/28/fw_faub-20230128@09:59:06     5.0G       0      1K     815       0  00:00:05  Day   4 hours, 17 minutes
    /var/mybackups/2023/01/28/fw_faub-20230128@10:00:32     5.0G       0      1K     815       0  00:00:05  Day   4 hours, 16 minutes
    /var/mybackups/2023/01/28/fw_faub-20230128@13:34:03     1.2G  488.3M      1K     815       3  00:00:11  Day   42 minutes, 45 seconds

    Size is the full size of the data in that backup. Used is is the actual disk space used to store it.
    Dirs is the number of directories.
    SymLks are the number of symlinks made - these equal the number of symlinks on the remote system being backed up.
    Mods is the number of modified files in that backup compared to the previous backup.

# EXAMPLES

## Single-File Backups
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

## Faub-Style Backups
**managebackups -p artemis --directory /opt/backups --faub "ssh artemis managebackups --path /usr/local/bin --path /etc" --prune --fp --save**
: Take a new faub-style backup of server artemis' /etc and /usr/local/bin directories, saving them locally to /opt/backups.  Prune older copies of this backup that have aged out.  Note: faub-backups assume **managebackups** is installed on the remote (in this case artemis) server. This example also assumes **managebackups** is installed suid-root on both servers, otherwise one or both invocations of **managebackups** would require sudo.

## General
**managebackups -1**
: Show details of all backups taken that are associated with each profile. Additionally **-p** [*profile*] could be specified to limit the output to a single profile.

**managebackups -0**
: Show a one-line summary for each backup profile. The summary includes detail on the most recent backup as well as the number of backups, age ranges and total disk space.

## Interrogate Backups
**managebackups -1 -p laptop**

    April 2023                                          Size  Used  Dirs  SymLks  Mods  Duration  Type  Age
    /var/backups/2023/04/14/laptop-2023-04-14@17:20:09  3.2G  962K    1K      26     6  00:00:54  Day   6 days, 23 hours
    /var/backups/2023/04/14/laptop-2023-04-14@17:57:12  3.2G  1.7M    1K      26    26  00:00:03  Day   6 days, 23 hours
    /var/backups/2023/04/21/laptop-2023-04-21@16:13:26  3.2G  4.8M    1K      26    31  00:00:03  Day   46 minutes, 44 seconds
    /var/backups/2023/04/21/laptop-2023-04-21@16:51:32  3.2G  4.5M    1K      26    29  00:00:13  Day   8 minutes, 28 seconds
    /var/backups/2023/04/21/laptop-2023-04-21@16:55:33  3.2G  622K    1K      26     1  00:00:28  Day   4 minutes, 7 seconds

**managebackups -p laptop --diff 21@16:55**

    [/var/backups/2023/04/21/laptop-2023-04-21@16:55:33]
    /var/managebackups/.git/refs/heads/master
    /var/managebackups/bin/managebackups
    /var/managebackups/obj/BackupCache.o
    /usr/local/bin/managebackups

**managebackups -p laptop --last**

    Identical output to above as this is a synonym for diffing the most recent backup to its immediate predecessor.  Additionally, if "laptop" is made the default profile (such as via **managebackups -p laptop --default --save**) the above command can be further simplified to **managebackups --last**.

**managebackups -p laptop --diff 17:2 --diff 55:53** 

    [/var/backups/2023/04/14/laptop-2023-04-14@17:20:09]
    [/var/backups/2023/04/21/laptop-2023-04-21@16:55:33]
    +192 [-]  /var/backups/2023/04/14/laptop-2023-04-14@17:57:12/var/managebackups/src/statistics.cc
    +118 [-]  /var/backups/2023/04/14/laptop-2023-04-14@17:57:12/var/managebackups/src/ipc.cc
    +213 [-]  /var/backups/2023/04/14/laptop-2023-04-14@17:57:12/var/managebackups/src/BackupCache.cc
    +16K [-]  /var/backups/2023/04/14/laptop-2023-04-14@17:57:12/usr/local/bin/managebackups

# DEPENDENCIES
**managebackups** uses three open-source libraries. These are statically compiled in under MacOS and dynamically linked under Linux.

- OpenSSL (1.1.1s) for calculation of MD5s
- pcre (8.45) for support of regular expressions
- pcre++ (0.9.5) as a C++ interface to pcre

# DISK USAGE
Disk usage is a nuanced concept.  Not only can it be reported in specific bytes (kilobytes, megabytes, etc) used, it can also be reported in disk blocks used, since a full block is the minimum allocatable space and anything less than that will use a full block anyway.  By default, the 'du' command reports in blocks.  Another complication is directory entries and symlinks (the directory itself, not its contents;  the symlink itself, not what it's pointing to).  They take up a small amount of space.  The stat() system call returns details on that space but for some reason the 'du' command ignores those.  **managebackups** provides two implmentations.  By default it reports specific bytes used.  Given the **--blocks** option, it shows the blocks used.  In both cases, the tiny amount of space used by directories and symlinks is ignored, again, like 'du'.

