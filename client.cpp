#include <sys/socket.h>
#include "helper.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

int main() {

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); //127.0.0.1
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

    if(rv < 0) {
        die("failed connection");
    }

    char wbuf[] = "hello?";
    write(fd, wbuf, strlen(wbuf));


    char rbuf[64]= {};
    ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
    if(n < 0) {
        die("read");
    }
    printf("server says: %s\n", rbuf);

    close(fd);

    return 0;
}
