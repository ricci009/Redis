#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include "helper.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdint>
#include <cassert>

const size_t k_max_msg = 4096;

static int32_t send_req(int fd, const char *text)
{
    uint32_t len = uint32_t(sizeof(text));
    if (len > k_max_msg)
    {
        std::cerr << "len too long\n";
        return -1;
    }
    char wbuf[4 + sizeof(text)];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);

    if (int32_t err = write_all(fd, wbuf, 4 + len))
    {
        return err;
    }
    return 0;
}

static int32_t read_res(int fd)
{
    size_t len;
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err == -1)
    {
        if (errno == 0)
        {
            std::cerr << "EOF\n";
        }
        else
        {
            std::cerr << "read() error\n";
        }
        return err;
    }
    memcpy(&len, rbuf, 4);

    std::cerr << "length: " << len << "\n";

    if (len > k_max_msg)
    {
        std::cerr << "too long\n";
        return -1;
    }
    err = read_full(fd, &rbuf[4], len);
    if (err)
    {
        std::cerr << "read() error\n";
    }

    rbuf[4 + len] = '\0';
    printf("server says: %s\n", &rbuf[4]);
    return 0;
}

int main()
{

    int status;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    status = getaddrinfo("localhost", "1234", &hints, &res);

    if (status == -1)
    {
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_flags);

    int32_t err = connect(fd, res->ai_addr, res->ai_addrlen);

    if (err == -1)
    {
        std::cerr << "ERROR CONNECTION\n";
    }

    const char *query_list[3] = {"hello1", "hello2", "hello3"};
    for (size_t i = 0; i < 3; i++)
    {
        err = send_req(fd, query_list[i]);
        if (err == -1)
        {
            goto L_DONE;
        }
    }

    for (size_t i = 0; i < 3; i++)
    {
        err = read_res(fd);
        if (err == -1)
        {
            goto L_DONE;
        }
    }

L_DONE:
    close(fd);
    return 0;
}
