#include "network.h"
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdint>
#include <thread>
#include <cassert>
#include <netdb.h>
#include <vector>

//TODO: handle single request needs to be configurated with read_full

int k_max_msg = 4096;

enum {
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2,
};

struct Conn {

    int fd = -1;
    uint32_t state = 0;

    //buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];

    //buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];

}

int get_listener_socket(void){

    int listener;
    int yes = 1;
    int rv;

    struct addrinfo hints, *ai, *p;

    //get the socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if((rv = getaddrinfo(NULL, "1234", &hints, &ai)) != 0){
        fprintf(stderr, "pollserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(listener < 0) {
            continue;
        }

        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if(bind(listener, p->ai_addr, p->ai_addrlen) < 0)
        {
            close(listener);
            continue;
        }
        break;
    }

    freeaddrinfo(ai);

    if(p == NULL) {
        return -1;
    }

    if(listen(listener,  10) == -1) {
        return -1;
    }

    return listener;
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
    if(fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
    struct sockaddr_in client_addr = {};
    socklen_t = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if(connfd < 0) {
        msg("accept() error");
        return -1;
    }
    fd_set_nb(connfd);

    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if(!conn){
        close(connfd);
        return -1;
    }
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}

static void connection_io(Conn *conn) {
    if(conn->state == STATE_REQ) {
        state_req(conn);
    } else if(conn->state == STATE_RES) {
        state_res(conn);
    }else {
        assert(0);
    }
}

static void state_req(Conn *conn) {
    while(try_fill_buffer(conn)) {}
}

static bool try_fill_buffer(Conn *conn) {
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while(rv < 0 && errno == EINTR);

    if(rv < 0 && errno == EAGAIN) {
        return false;
    }

    if(rv < 0) {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }

    if(rv == 0) {
        if(conn->rbuf_size > 0) {
            msg("unexpected EOF");
        }
        else {
            msg("EOF");
        }
        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size +=  (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    while(try_one_request(conn)) {}
    return (conn->state == STATE_REQ);
}

int32_t handle_write_request(int connfd) {
    int32_t err;
    uint32_t len;
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = uint32_t(sizeof(reply));
    memcpy(wbuf, &len, 4); //memcpy(dest, src, len)
    memcpy(&wbuf[4], reply, len);
    err = write_all(connfd, wbuf, len + 4);
    if(err == -1) {
        std::cerr << "writing error\n";
    }
    return err;
}

int32_t handle_read_request(int connfd) {
    //parsing request, handling request, etc...

    //need to create the buffer
    //4 for identifying len of message (4 bytes total)
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if(err == -1) {
        if(errno == 0) {
            std::cerr << "EOF\n";
        }
        else {
            std::cerr << "read() error\n";
        }
        return err;
    }

    uint32_t len = 0;
    //len is now included in rbuf, need to extract it. Could use regex probably not necessary.
    //how do you turn rbuf contents into a uint32_t...
    //memcpy copies bytes from one location to the other!
    memcpy(&len, rbuf, 4);
    if(len > k_max_msg) {
        std::cerr << "msg length too long\n";
        return -1;
    }

    err = read_full(connfd, &rbuf[4], len);
    if(err) {
        std::cerr << "message read error\n";
        return -1;
    }

    //NEVER FORGET TO ADD THE NULL TERMINATOR!!!!!
    rbuf[4 + len] = '\0';

    printf("client says: %s\n", &rbuf[4]);
    //need to reply with same protocol
    return 0;
}

void *get_in_addr(struct sockaddr *sa) {
    if(sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void add_to_pfds(struct pollfd **pfds, int newfd, int *fd_count, int *fd_size) {
    //no room -> add more space to pfds array
    if(*fd_count == *fd_size) {
        *fd_size *= 2;
        *pfds = (pollfd *)realloc(*pfds, sizeof(pfds) * (size_t)(*fd_size));
    }

    (*pfds)[*fd_count].fd = newfd;
    (*pfds)[*fd_count].events = POLLIN;

    (*fd_count)++;
}

void del_from_pfds(struct pollfd *pfds, int i, int *fd_count) {
    pfds[i] = pfds[*fd_count - 1];

    (*fd_count)--;
}





void init_connection() {
    //create a listening socket that will then be used as the initial poll
    //add to set of file descriptors passed into poll
    int err;

    int listener;

    //new client setup
    int newfd;
    struct sockaddr_storage remoteaddr;
    socklen_t addrlen;

    char remoteIP[INET6_ADDRSTRLEN];

    //set up and get a listening socket
    listener = get_listener_socket(); //returns listener fd

    if(listener == -1) {
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }

    fd_set_nb(listener);

    std::vector<Conn *> fd2conn;

    std::vector<struct pollfd> poll_args;

    //MAIN LOOP
    while(1) {
        poll_args.clear();

        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        for (Conn *conn: fd2conn) {
            if(!conn) {
                continue;
            }
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        //poll for active fds
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if(rv < 0) {
            die("poll");
       }


        //process active connections
        for (size_t i = 1; i < poll_args.size(); i++) {
            if(poll_args[i].revents) {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if(conn->state == STATE_END){
                    //close connection
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }

        if(poll_args[0].revents) {
            (void)accept_new_conn(fd2conn, fd);
        }
    }
    return 0;
}
