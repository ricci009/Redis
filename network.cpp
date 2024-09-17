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

const size_t k_max_msg = 4096;

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

    return listener;
}

static void conn_put(std::unordered_map<int, Conn *> &fd2conn, struct Conn *conn)
{
    // if (fd2conn.size() <= (size_t)conn->fd)
    // {
    //     fd2conn.resize(conn->fd + 1);
    // }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::unordered_map<int, Conn *> &fd2conn, int fd)
{
    struct sockaddr_in client_addr = {};
    socklen_t size = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &size);
    if (connfd < 0)
    {
        std::cerr << "accept() error\n";
        return -1;
    }

    fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL) | O_NONBLOCK);
    // fd_set_nb(connfd); //TODOb: implement this;

    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn)
    {
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

int try_one_request(Conn *conn)
{
    if (conn->rbuf_size < 4)
    {
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);

    if (len > k_max_msg)
    {
        return false;
    }

    if (4 + len > conn->rbuf_size)
    {
        return false; // not enough data in buffer... yet...
    }

    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    // echo the response
    memcpy(conn->wbuf, conn->rbuf, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;

    // now the rbuf needs to be altered
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain > 0)
    {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }

    conn->rbuf_size = remain;

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
    do
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 && errno == EINTR);

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
        if (conn->rbuf_size > 0)
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

    int listener;

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

    fcntl(listener, F_SETFL, fcntl(listener, F_GETFL) | O_NONBLOCK); // set listener to non blocking

    std::unordered_map<int, Conn *> fd2conn;

    std::vector<struct pollfd> poll_args;

    // MAIN LOOP
    while (1)
    {

        poll_args.clear();

        struct pollfd pfd = {listener, POLLIN, 0};
        poll_args.push_back(pfd);

        for (auto conn: fd2conn)
        {
            struct pollfd pfd = {};
            pfd.fd = conn.second->fd;
            pfd.events = (conn.second->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        // poll for active fds
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 100);
        if (rv < 0)
        {
            die("poll");
        }

        // process active connections
        for (size_t i = 1; i < poll_args.size(); i++) //TODO: fix segfault in this loop..
        {
            if (poll_args[i].revents)
            {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END)
                {
                    // close connection
                    fd2conn.erase(conn->fd);
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }

        if (poll_args[0].revents)
        {
            (void)accept_new_conn(fd2conn, listener); // TODO: FIX THIS ERROR!
        }
    }
}
