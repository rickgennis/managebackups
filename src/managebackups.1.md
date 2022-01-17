% MANAGEBACKUPS(1) managebackups 3.0.2
% Rick Ennis
% January 2022

# NAME
managebackups - Take and manage backups

# SYNOPSIS
**managebackups** [*OPTION*]

# DESCRIPTION
**managebackups** provides three functions that can be run independently or in combination:

## Take Backups
Given a backup command (tar, cpio, dump, etc) **managebackups** will execute the command, saving the output to a file named with the current date (and optionally time).  By default the resulting filename will be of the form *directory*/YYYY/MM/*filename*-YYYYMMDD.*ext*.  When time is included the day of month is also added to the directory structure (*directory*/YYYY/MM/DD/*filename*-YYYYMMDD-HH::MM:SS.*ext*). 

## Prune Backups
**managebackups** deletes old backups that have aged out.  The aging critera is configured on a daily, weekly, monthly and yearly basis.  By default *managebackups* will keep 14 dailies, 4 weeklies, 6 monthlies and 2 yearly backups.

## Hard Linking
In setups where all backups are fulls, and therefore many are potentially identical, *managebackups* can save disk space by hard linking identical copies together.  This is done by default when identical copies are identified. 

# PROFILES
Backup profiles are a collection of settings describing a backup set -- its directory to save backups to, the command to take the backups, how many weekly copies to keep, etc.  Once a profile is associated with a collection of options, all of those options are invoked when the profile is specified, unless an overriding option is also given.

# OPTIONS
Options are relative to the three functions of **managebackups**.

## General Options
**--help**
: Displays help text.

**-v**
: Provide more verbose output (can be specified several times for debug-level detail).

**-p**, **--profile** [*PROFILE*]
: Use *PROFILE* for the current run.  

**--save**
: Save the currently specified settings (everything on the command line) with the specified title.

**-0**
: Provide a summary of backups.

**-1**
: Provide detail of backups.

**--notify**
: Notify after a backup completes.

## Take Backups Options

**--directory**
: Specify the directory in which to store and look for backups.

**--file**
: Specify the base filename to create for new backups.  The date and optionally time are inserted before the extension, or if no extension, at the end.  A filename of mybackup.tgz will become mybackup-YYYYMMDD.tgz.

**--time**
: Include the time in the filename of the newly created backup.  The day of month will also be included in the subdirectory.

# NOTIFICATIONS
**managebackups** can notify on success or failure of a backup via two methods: email or script. Multiple emails and/or scripts can be specified for the same backup.

## Email Notifications
Notifications are sent to all email addresses configured for the current profile on every failure.  Notifications are only sent on successes if Notify On Success (**--nos**) is also specified.

## Script Notifications
Notification scripts configured for the current profile are only considered on a state change. A state change is defined as a backup succeeding or failing when it did the opposite in its previous run. On a state change, all notification scripts for the profile will be executed if the backup failed.  State changes that change to success are only notified if Notify On Success (**--nos**) is also specified. In effect, this means the script(s) will only be called for the first in a string of failures or, with **--nos**, a string of successes.

Notification scripts are passed a single parameter, which is a message describing details of the backup event.



