#include <iostream>
#include <exception>
#include <string>
#include <array>
#include <string_view>
#include <cstring>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

template <const std::string_view& S1, const std::string_view& S2>
struct concat {
    static constexpr auto init() noexcept {
        constexpr std::size_t len = S1.size() + S2.size() + 1;
        std::array<char, len> ar{};
        auto append = [i=0, &ar](const auto& s) mutable { for (auto e : s) ar.at(i++) = e; };
        append(S1);
        append(S2);
        ar[len-1] = 0;
        return ar;
    }
    static constexpr auto arr = init();
    static constexpr std::string_view value{arr.data(), arr.size()-1};
};

template <const std::string_view& S>
void throw_exception() {
    static constexpr std::string_view colon_space = ": ";
    throw std::runtime_error(std::string(concat<S, colon_space>::value) + std::strerror(errno));
}

struct auto_close_fd {
    int fd;
    operator int() {return fd;}
    auto_close_fd(int fd): fd(fd) {}
    auto_close_fd(const auto_close_fd&) = delete;
    auto_close_fd(auto_close_fd&&) = delete;
    auto_close_fd& operator=(const auto_close_fd&) = delete;
    auto_close_fd& operator=(auto_close_fd&&) = delete;
    ~auto_close_fd() {if (fd != -1) close(fd);}
};

int main() {
    try {
        auto_close_fd epfd = epoll_create1(0);
        static constexpr std::string_view epoll_create1_failed = "epoll_create1 failed";
        if (epfd < 0) throw_exception<epoll_create1_failed>();

        epoll_event ev{};
        ev.data.fd = STDIN_FILENO;
        ev.events = EPOLLIN;
        static constexpr std::string_view epoll_ctl_failed = "epoll_ctl failed";
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0) throw_exception<epoll_ctl_failed>();
        
        std::array<epoll_event,1> evs{};
        constexpr std::size_t TIMEOUT_MS = 10000;
        auto len = epoll_wait(epfd, evs.data(), sizeof(evs)/sizeof(evs[0]), TIMEOUT_MS);
        
        static constexpr std::string_view epoll_wait_failed = "epoll_wait failed";
        if (len < 0) throw_exception<epoll_wait_failed>();
        else if (len == 0) cout << "timeout(10 sec)" << endl;
        else {
            static constexpr std::string_view epoll_wait_returned_multiple_fds = "epoll_wait returned multiple fds";
            if (len != 1) throw_exception<epoll_wait_returned_multiple_fds>();
            static constexpr std::string_view epoll_wait_returned_wrong_fd = "epoll_wait returned wrong fd";
            if (evs[0].data.fd != STDIN_FILENO) throw_exception<epoll_wait_returned_wrong_fd>();
            constexpr std::size_t BUFFER_SIZE = 10;
            std::array<char,BUFFER_SIZE> buffer{};
            auto read_size = read(evs[0].data.fd, buffer.data(), buffer.size());
            static constexpr std::string_view read_failed = "read failed";
            if (read_size < 0) throw_exception<read_failed>();
            cout << "read: \"" << string(buffer.data(), read_size) << "\"" << endl;
        }
    }
    catch(exception& s) {
        cerr << s.what() << endl;
        return 1;
    }
    return 0;
}