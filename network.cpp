#include "network.h"
#include <sys/socket.h>
#include "helper.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

void init_connection() {

    int fd = socket(AF_INET, SOCK_STREAM, 0); //create the socket that will be used for interactions with client and server
    int val = 1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); //set the socket for TCP

    //bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr)); //bind is used to associate the socket created that lives in namespace with an address. (RV)
    if(rv) {
        die("bind()");
    }

    rv = listen(fd, SOMAXCONN);
    if(rv) {
        die("listen()");
    }

    while(true) {
        //accept
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen); //open up the socket "FD" to be able to accept connections. Builds a queue
        if(connfd < 0) {
            continue; //error
        }

        handle_connection(connfd);
        //aka recv from that connfd.... if I am not mistaken!
        //aka allocate thread to handle connection or something else.
        close(connfd);
    }
}

void handle_connection(int connfd) {
    //need to create a buffer that will be read in
    //recv or read
    char rbuf[64] = {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf - 1)); //stores count of bytes ssize_t also can have -1 for errors.
    if(n < 0) {
        die("read() error");
        return;
    }
    printf("client says: %s\n", rbuf);

    char wbuf[] = "world"; //can also use vector<char> for more complicated handling... regex for parsing???
    write(connfd, wbuf, strlen(wbuf));
}
