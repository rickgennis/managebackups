MANAGEBACKUPS


=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

Managebackups can manage backups created by other utilities (tar, dump, cpio, etc) or create its own.
Management consists of applying a retention policy (deleting backups as they age out based on daily/
weekly/monthly/yearly criteria) and hard linking identifical copies together to save disk space.

Creating it's own backups can be in one of two forms:

    (1) single-file backups - These are analogous to tar, dump, cpio.
    (2) faub-style backups - These are faster to generate and more easily manipulated (see man page).


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
