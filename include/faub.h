#include <string>
#include <vector>
#include "BackupConfig.h"
#include "ipc.h"

using namespace std;


string mostRecentBackupDir(string backupDir);
string newBackupDir(string backupDir);
void fs_serverProcessing(PipeExec& client, BackupConfig& config, string prevDir, string currentDir);
void fs_startServer(BackupConfig& config);
size_t fc_scanToServer(string entryName, IPC_Base& server);
size_t fc_sendFilesToServer(IPC_Base& server);
void fc_mainEngine(BackupConfig& config, vector<string> paths);
void pruneFaub(BackupConfig& config);
