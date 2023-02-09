
#ifndef SETUP_H
#define SETUP_H

void installman();
void install(string myBinary, bool suid = false);
void scheduleRun();
void scheduleLaunchCtl(string& appPath);
void scheduleCron(string& appPath);

#endif

