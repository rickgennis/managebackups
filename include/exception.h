
#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <string>

using namespace std;


class MBException : public std::exception {
    string message;

    public:
        MBException(string msg) : message(msg) {}

        string detail() { return message; }
};


#endif

