
#ifndef FS_H 
#define FS_H 

#include <string>

#include "network.h"


using namespace std;

void c_scanToServer(string directory, tcpSocket& server);
void c_sendFilesToServer(tcpSocket& server);


#endif

