#include <cstring>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <cstdlib>

inline void die(const char *msg) {
    std::cerr << msg << "\n";
    exit(1);
}
