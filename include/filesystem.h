
#ifndef FS_H 
#define FS_H 

#include <string>

#include "network.h"


using namespace std;

void fc_scanToServer(string directory, tcpSocket& server);
void fc_sendFilesToServer(tcpSocket& server);


#endif

