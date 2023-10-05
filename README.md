MANAGEBACKUPS

managebackups provides three functions that can be run independently or in combination:
      
   1. Take Backups
       Backups can be configured in one of two forms:

       • Single file: A single file backup is any of the standard Linux backup commands (tar, cpio, dump) that result in a single compressed file.  Given a backup command (tar, etc) managebackups will execute the
         command, saving the output to a file named with the current date (and optionally time).  By default the resulting filename will be of the form directory/YYYY/MM/filename-YYYY-MM-DD.ext.  When time is included
         the day of month is also added to the directory structure (directory/YYYY/MM/DD/filename-YYYY-MM-DD-HH::MM:SS.ext).  Note: Without time included (–time) multiple backups on the same day taken with the same
         settings will overwrite each other resulting in a single backup for the day.  With time included each backup is saved separately.

       • Faubackup: Faub-style backups, similar to the underlying approach of Apple’s Time Machine, backs up an entire directory tree to another location without compression.  The initial copy is an exact replica of
         the source.  Subsequent copies are made to new directories but are hard-linked back to files in the the previous backup if the data hasn’t changed.  In effect, you only use disk space for changes but get the
         advantage of fully traversable directory trees, which allows interrogation via any standard commandline tool.  Each backup creates a new directory of the form directory/YYYY/MM/profile-YYYY-MM-DD.  With the
         –time option @HH:MM:SS gets appended as well.  As an example, determining when /etc/passwd was changed with faub backups can be as simple as using ls:

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

   2. Prune Backups
       managebackups deletes old backups that have aged out.  The retention critera is configured on a daily, weekly, monthly and yearly basis.  By default managebackups will keep 14 daily, 4 weekly, 6 monthly and 2
       yearly backups.  Additionally, managebackups can perform a second level of pruning called consolidation.  If elected, multiple backups taken on the same day can be consolidated down to a single per day backup
       after said backup has reached a specified age.  The default is no consolidation.

   3. Hard Linking
       In configurations using a single file backup where all backups are fulls, and therefore many are potentially identical, managebackups can save disk space by hard linking identical copies together.  This is done
       by default when identical copies are identified.  In Faub-style backups hard linking is automatically implemented on a per-file basis.


=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

INSTALLING

    - If there's any chance you'll want to do faub-style backups managebackups needs to run as root; use:
        sudo managebackups --installsuid

    - If you only want to create single-file backups or manage existing ones you can use:
        sudo managebackups --install


=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

GENERATE A PROFILE AND START BACKING UP

Single-file Backups

    managebackups -p myvar --directory /usr/local/backups -f var.tgz -c "tar czf - /var" --save

This backs up /var to a file named /usr/local/backups/YEAR/MONTH/var-MONTH-DAY-YEAR.tgz
while creating a profile for these settings called myvar.  You can then run the same command again via

        managebackups -p myvar


Faub-style Backups

    managebackups -p homes --directory /usr/local/backups --faub "managebackups -s /home" --time --save

This backs up /home to a directory under /usr/local/backups/YEAR/MONTH/DAY/homes-MONTH-DAY-YEAR@HH::MM:SS
while creating a profile for these settings called myvar.  You can then run the same command again via

        managebackups -p homes

Settings specified with a profile (-p) and the --save option are invoked any time that profile (-p) is
specified on future invocations.  If additional and/or different options are given with that profile
name on subsequent runs, the new options override the ones saved in the profile just for that run.
If --save is given again then they override permanently.

=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

Use "man managebackups" for full details (managebackups.1 before installation).

==================================================

COMPILING
---------
My Makefile and cross-architecture skills are pretty weak.  I have it compiling under MacOS and Debian
but that requires specific dependencies that aren't appropriately checked for (see the CREDITS file).
