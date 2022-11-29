#include <string>
#include <vector>
#include "BackupConfig.h"
#include "network.h"

using namespace std;


string mostRecentBackupDir(string backupDir);
string newBackupDir(string backupDir);
void fs_serverProcessing(tcpSocket& client, vector<BackupConfig>& configs, string prevDir, string currentDir);
void fs_startServer(vector<BackupConfig>& configs, string dir);
void fc_scanToServer(string directory, tcpSocket& server);
void fc_sendFilesToServer(tcpSocket& server);
void fc_mainEngine();

