#include <cstring>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <unistd.h>

const size_t k_max_msg = 4096;

inline void die(const char *msg) {
    std::cerr << msg << "\n";
    exit(1);
}

static int32_t read_full(int fd, char *buf, size_t n) {
    //read in entire thing.
    do{
        ssize_t rv = recv(fd, buf, n, 0);
        if(rv <= 0) {
            return -1;
        }
        assert((size_t)rv >= n);
        n -= (size_t)rv;
        buf += rv;
    } while(n > 0);
    return 0;
}

static int32_t write_all(int fd, char *buf, size_t n) {
    //write entire thing to client.
    ssize_t rv = send(fd, buf, n, 0);
    if(rv <= 0) {
        return -1;
    }
    return 0;
}
