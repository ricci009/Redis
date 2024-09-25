#define MAX_EVENTS 5
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
#include <sys/fcntl.h>
#include <unordered_map>
#include <sys/epoll.h>

const size_t k_max_msg = 4096;

//TODO: invalid size and double free or curroption


enum
{
    STATE_REQ = 0, // request
    STATE_RES = 1, // response
    STATE_END = 2, // closed
};

struct Conn
{

    int fd = -1;
    uint32_t state = 0;

    // buffer for reading
    size_t rbuf_size = 0;
    size_t rbuf_read = 0;
    uint8_t rbuf[4 + k_max_msg];

    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

static void state_res(Conn *conn);

static void state_req(Conn *conn);

int get_listener_socket(void);

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn);

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd);

int try_one_request(Conn *conn);

static bool try_fill_buffer(Conn *conn);

static bool try_flush_buffer(Conn *conn);

static void connection_io(Conn *conn);

int get_listener_socket(void)
{

    int listener;
    int yes = 1;
    int rv;

    struct addrinfo hints, *ai, *p;

    // get the socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, "1234", &hints, &ai)) != 0)
    {
        fprintf(stderr, "pollserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next)
    {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0)
        {
            continue;
        }

        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0)
        {
            close(listener);
            continue;
        }
        break;
    }

    freeaddrinfo(ai);

    if (p == NULL)
    {
        return -1;
    }

    if (listen(listener, 10) == -1)
    {
        return -1;
    }

    fcntl(listener, F_SETFL, fcntl(listener, F_GETFL) | O_NONBLOCK); // set listener to non blocking

    return listener;
}

static int32_t accept_new_conn(std::unordered_map<int, Conn *> &fd2conn, int connfd)
{
    fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL) | O_NONBLOCK); //set to non-blocking

    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn)
    {
        close(connfd);
        return -1;
    }
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->rbuf_read = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    fd2conn[connfd] = conn;
    return 0;
}

int try_one_request(Conn *conn)
{
    if (conn->rbuf_size - conn->rbuf_read < 4)
    {
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[conn->rbuf_read], 4);
    conn->rbuf_read += 4;


    if (len > k_max_msg)
    {
        return false;
    }

    if (len + conn->rbuf_read > conn->rbuf_size)
    {
        return false; // not enough data in buffer... yet...
    }

    printf("client says: %.*s\n", len, &conn->rbuf[conn->rbuf_read]);

    // echo the response

    memcpy(conn->wbuf, &conn->rbuf[conn->rbuf_read - 4], 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[conn->rbuf_read], len);

    conn->wbuf_size = 4 + len;
    conn->rbuf_read += len;

    conn->state = STATE_RES;

    state_res(conn);

    return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn))
    {
    }
}

static bool try_fill_buffer(Conn *conn)
{
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;

    if(conn->rbuf_read > conn->rbuf_size) {

}

    memmove(conn->rbuf, &conn->rbuf[conn->rbuf_read], conn->rbuf_size - conn->rbuf_read);
    conn->rbuf_size = conn->rbuf_size - conn->rbuf_read;
    conn->rbuf_read = 0;

    do
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 && errno == EINTR); //what I am confused about is why not just read it all in here?

    if (rv < 0 && errno == EAGAIN)
    {
        return false;
    }

    if (rv < 0)
    {
        conn->state = STATE_END;
        return false;
    }

    if (rv == 0)
    {
        if (conn->rbuf_size - conn->rbuf_read > 0)
        {
            std::cerr << "EOF reached early\n";
        }
        else
        {
            std::cerr << "EOF\n";
        }
        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    while (try_one_request(conn))
    {
    }
    return (conn->state == STATE_REQ);
}

static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn))
    {
    }
}

static bool try_flush_buffer(Conn *conn)
{
    ssize_t rv = 0;

    do
    {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);

    if (rv < 0)
    {
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;

    assert(conn->wbuf_sent <= conn->wbuf_size);

    if (conn->wbuf_sent == conn->wbuf_size)
    {
        // response fully sent, change state back
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }

    return true;
}

static void connection_io(Conn *conn)
{
    if (conn->state == STATE_REQ)
    {
        state_req(conn);
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn);
    }
    else
    {
        assert(0);
    }
}

void init_connection()
{
    // create a listening socket that will then be used as the initial poll
    // add to set of file descriptors passed into poll
    int err;

    int listener, event_count, clientfd;

    // new client setup
    int newfd;
    struct sockaddr_storage remoteaddr;
    socklen_t addrlen;

    char remoteIP[INET6_ADDRSTRLEN];

    // set up and get a listening socket
    listener = get_listener_socket(); // returns listener fd

    if (listener == -1)
    {
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }



    struct epoll_event event, events[MAX_EVENTS];

    int epoll_fd = epoll_create1(0);

    if(epoll_fd == -1) {
        fprintf(stderr, "failed to create epoll file descriptor\n");
        exit(1);
    }

    event.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
    event.data.fd = listener;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener, &event)) {
        fprintf(stderr, "Failed to add file descriptor to epoll\n");
    }

    std::unordered_map<int, Conn *> fd2conn;

    std::vector<struct pollfd> poll_args;

    int index = 0;
    int res;

    // MAIN LOOP
    while (1)
    {
        res = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if(res == -1) {
            perror("epoll_wait");
            exit(1);
        }

        for (int index = 0; index < res; ++index) {
            clientfd = events[index].data.fd;

            if(clientfd == listener) {
                struct sockaddr_in client_addr = {};
                socklen_t size = sizeof(client_addr);
                int newfd;
                if((newfd = accept(listener, (struct sockaddr *)&client_addr, &size)) == -1) {
                    perror("Server-accept() error");
                }
                else {
                    (void)accept_new_conn(fd2conn, newfd);
                    event.events = POLLIN;
                    event.data.fd = newfd;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, newfd, &event) < 0) {
                        perror("epoll_ctl");
                        exit(1);
                    }
                }
                break; //new connection added to epoll_fd. reiterate loop with new connection.
            }
            else {
                if(events[index].events & EPOLLIN) {
                    assert(fd2conn.find(clientfd) != fd2conn.end());
                    Conn *conn = fd2conn[clientfd];
                    connection_io(conn);
                    if(conn->state == STATE_END) {
                        fd2conn.erase(conn->fd);
                        (void)close(conn->fd); //my assupmtion is it is closing the listener, no bueno.
                        free(conn);
                        conn = NULL;
                        //should I break right here?
                    }
                }
            }
        }
    }
}
