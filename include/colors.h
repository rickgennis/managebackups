
#ifndef COLORS_H
#define COLORS_H

#define ifcolor(x) (GLOBALS.color && NOTQUIET ? x : "")

#define RESET   ifcolor("\033[0m")
#define BLACK   ifcolor("\033[30m")     /* Black */
#define RED     ifcolor("\033[31m")     /* Red */
#define GREEN   ifcolor("\033[32m")      /* Green */
#define YELLOW  ifcolor("\033[33m")      /* Yellow */
#define BLUE    ifcolor("\033[34m")      /* Blue */
#define MAGENTA ifcolor("\033[35m")      /* Magenta */
#define CYAN    ifcolor("\033[36m")      /* Cyan */
#define WHITE   ifcolor("\033[37m")      /* White */
#define BOLDBLACK   ifcolor("\033[1m\033[30m")      /* Bold Black */
#define BOLDRED     ifcolor("\033[1m\033[31m")      /* Bold Red */
#define BOLDGREEN   ifcolor("\033[1m\033[32m")      /* Bold Green */
#define BOLDYELLOW  ifcolor("\033[1m\033[33m")      /* Bold Yellow */
#define BOLDBLUE    ifcolor("\033[1m\033[34m")      /* Bold Blue */
#define BOLDMAGENTA ifcolor("\033[1m\033[35m")      /* Bold Magenta */
#define BOLDCYAN    ifcolor("\033[1m\033[36m")      /* Bold Cyan */
#define BOLDWHITE   ifcolor("\033[1m\033[37m")      /* Bold White */

#endif

