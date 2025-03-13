#include <iostream>
#include <exception>
#include <string>
#include <cstring>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

#define THROW_EXCEPTION(s) throw runtime_error(string(s ": ") + strerror(errno))

struct auto_close_fd {
    int fd;
    operator int() {return fd;}
    auto_close_fd(int fd): fd(fd) {}
    ~auto_close_fd() {if (fd != -1) close(fd);}
};

int main() {
    try {
        auto_close_fd epfd = epoll_create1(0);
        if (epfd < 0) THROW_EXCEPTION("epoll_create1 failed");

        epoll_event ev;
        ev.data.fd = STDIN_FILENO;
        ev.events = EPOLLIN;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0) THROW_EXCEPTION("epoll_ctl failed");
        
        epoll_event evs[1];
        auto len = epoll_wait(epfd, evs, sizeof(evs)/sizeof(evs[0]), 10000);
        
        if (len < 0) THROW_EXCEPTION("epoll_wait failed");
        else if (len == 0) cout << "timeout(10 sec)" << endl;
        else {
            if (len != 1) THROW_EXCEPTION("epoll_wait returned multiple fds");
            if (evs[0].data.fd != STDIN_FILENO) THROW_EXCEPTION("epoll_wait returned wrong fd");
            char buffer[10];
            auto read_size = read(evs[0].data.fd, buffer, sizeof(buffer));
            if (read_size < 0) THROW_EXCEPTION("read failed");
            cout << "read: \"" << string(buffer, read_size) << "\"" << endl;
        }
    }
    catch(exception& s) {
        cerr << s.what() << endl;
        return 1;
    }
    return 0;
}