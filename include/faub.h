#include <string>
#include <vector>
#include "BackupConfig.h"
#include "PipeExec.h"
#include "network.h"

using namespace std;


string mostRecentBackupDir(string backupDir);
string newBackupDir(string backupDir);
void fs_serverProcessing(PipeExec& client, BackupConfig& config, string prevDir, string currentDir);
void fs_startServer(BackupConfig& config);
void fc_scanToServer(string directory, tcpSocket& server);
void fc_sendFilesToServer(tcpSocket& server);
void fc_mainEngine(vector<string> paths);
void pruneFaub(BackupConfig& config);
