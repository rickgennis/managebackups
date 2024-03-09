
#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <string>

using namespace std;


class MBException : public std::exception {
    string message;
    string data;
    
public:
    MBException(string msg) : message(msg) {}
    MBException(string msg, string d) : message(msg), data(d) {}

    string detail() { return message; }
    string getData() { return data; }
};


#endif

