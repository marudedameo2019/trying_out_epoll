#include <iostream>
#include <vector>
#include <functional>
#include <string>
#include <chrono>
#include <map>
#include <unordered_map>
#include <utility>
#include <string_view>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>
class file_descriptor {
    int fd_;
public:
    constexpr static int INVALID = -1;
    file_descriptor(int fd = INVALID): fd_(fd) {}
    file_descriptor(file_descriptor &&org) noexcept: fd_(org.fd_) {org.fd_ = INVALID;}
    file_descriptor(const file_descriptor &org) = delete;
    file_descriptor &operator=(file_descriptor &&org) noexcept {
        if (this != &org) {
            this->~file_descriptor();
            this->fd_ = org.fd_;
            org.fd_ = INVALID;
        }
        return *this;
    }
    file_descriptor &operator=(const file_descriptor &org) = delete;
    ~file_descriptor() {
        if (this->fd_ != INVALID)
            close(this->fd_);
        this->fd_ = INVALID;
    }
    operator int() {return this->fd_;}
};
class epoll {
    file_descriptor fd_;
    constexpr static size_t MAX_EVENTS = 5;
    std::vector<epoll_event> events_;
    epoll(const epoll &) = delete;
    epoll(epoll &&) = delete;
    auto &operator=(const epoll &) = delete;
    auto &operator=(epoll &&) = delete;
    int ctl(int fd, int op, uint32_t events) {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        return epoll_ctl(this->fd_, op, fd, &ev);
    }
public:
    epoll(): fd_(epoll_create1(0)), events_(MAX_EVENTS) {}
    ~epoll() {}
    operator bool() {return static_cast<int>(this->fd_) != file_descriptor::INVALID;}
    int ctl_add(int fd, uint32_t events){return ctl(fd, EPOLL_CTL_ADD, events);}
    int ctl_mod(int fd, uint32_t events){return ctl(fd, EPOLL_CTL_MOD, events);}
    int ctl_del(int fd, uint32_t events){return ctl(fd, EPOLL_CTL_DEL, events);}
    int wait(int timeout) {
        this->events_.resize(MAX_EVENTS);
        auto len = epoll_wait(this->fd_, this->events_.data(), static_cast<int>(this->events_.size()), timeout);
        this->events_.resize(len < 0 ? 0 : len);
        return len;
    }
    const auto &events() const {return this->events_;}
};
class event_loop {
    epoll ep_;
    bool running_ = false;
    file_descriptor fd_;
    std::map<std::chrono::high_resolution_clock::time_point, std::function<void()>> time2fnc;
    std::unordered_map<int, std::function<void(int)>> fd2func;
    union buff8bytes {
        char bytes[8]; // NOLINT(cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-avoid-c-arrays)
        uint64_t value;
    };
    void interrupt() {
        buff8bytes buff; // NOLINT(cppcoreguidelines-pro-type-member-init)
        buff.value = 1; // NOLINT(cppcoreguidelines-pro-type-union-access)
        write(this->fd_, &buff.bytes[0], sizeof(buff.bytes)); // NOLINT(cppcoreguidelines-pro-type-union-access)
        // epoll_waitを止めるための特殊な書き込みなので結果を無視する
    }
public:
    event_loop(): fd_(eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE)) {
        this->ep_.ctl_add(this->fd_, EPOLLIN);
    }
    auto set_timeout(std::function<void()> func, int delay_ms) {
        using namespace std;
        using namespace std::chrono;
        auto t = high_resolution_clock::now() + milliseconds(delay_ms);
        auto it = this->time2fnc.find(t);
        while (it != this->time2fnc.end())
        {
            t += nanoseconds(1);
            it = this->time2fnc.find(t);
        }
        this->time2fnc.insert(std::make_pair(t, func));
    }
    auto register_fd(int fd, std::function<void(int)> func) {
        this->fd2func.insert(std::make_pair(fd, func));
        this->ep_.ctl_add(fd, EPOLLIN);
    }
    auto loop() {
        using namespace std;
        using namespace std::chrono;
        this->running_ = true;
        while (this->running_) {
            auto t = high_resolution_clock::now();
            auto it = this->time2fnc.begin();
            while (it != this->time2fnc.end()) {
                if (it->first < t) {
                    auto func = it->second;
                    this->time2fnc.erase(it);
                    func();
                } else {
                    break;
                }
                it = this->time2fnc.begin();
            }
            auto ms = (it == this->time2fnc.end()) ? -1 : duration_cast<milliseconds>(it->first - t).count();
            auto len = this->ep_.wait(static_cast<int>(ms));
            if (len > 0) {
                for (const auto &e : this->ep_.events()) {
                    if (e.data.fd == this->fd_) {
                        buff8bytes buff{};
                        read(e.data.fd, &buff.bytes[0], sizeof(buff.bytes)); // NOLINT(cppcoreguidelines-pro-type-union-access)
                        // カウンタをクリアするための特殊な読み込みなので結果を無視する
                    } else {
                        this->fd2func[e.data.fd](e.data.fd);
                    }
                }
            }
        }
    }
    void stop() {
        this->running_ = false;
        interrupt();
    }
};
int main() {
    using namespace std;
    using namespace std::chrono;
    event_loop loop;
    function<void(int)> on_read_request = [&loop](int fd)->void {
        constexpr int BUFF_SIZE = 10;
        vector<char> buff(BUFF_SIZE);
        auto len = read(fd, buff.data(), buff.size());
        buff.resize(len < 0 ? 0 : len);
        cout << "read(): ";
        cout.write(buff.data(), static_cast<streamsize>(buff.size()));
        cout << "\n" << endl;
        if (string(buff.data(), buff.size()) == "stop\n") {
            loop.stop();
        }
    };
    loop.register_fd(STDIN_FILENO, on_read_request);
    constexpr int WAIT_TIME_MS = 10000;
    auto t = high_resolution_clock::now();
    function<void()> on_timeout = [&loop,&on_timeout,&t]()->void {
        cout << WAIT_TIME_MS << "ms!\n" << endl;
        t = high_resolution_clock::now();
        loop.set_timeout(on_timeout, WAIT_TIME_MS);
    };
    loop.set_timeout(on_timeout, WAIT_TIME_MS);
    function<void()> on_draw = [&loop,&on_draw,&t]()->void {
        static constexpr string_view pre_line = "\x1b[s\x1b[1A\r";
        static constexpr string_view post_line = "\x1b[u";
        static constexpr array<string_view, 8> rest_chars = {" ", "\u258F", "\u258E", "\u258D", "\u258C", "\u258B", "\u258A", "\u2589"};
        auto t2 = high_resolution_clock::now();
        auto ms = duration_cast<milliseconds>(t2-t).count();
        constexpr int width100 = 80;
        auto char8size = ms * width100 * static_cast<int>(rest_chars.size()) / WAIT_TIME_MS;
        auto chars = char8size / static_cast<int>(rest_chars.size());
        auto rest = char8size % static_cast<int>(rest_chars.size());
        cout << pre_line;
        for (int i = 0; i < chars; ++i)
            cout << "\u2588";
        cout << rest_chars.at(rest);
        cout << ms << "[ms]";
        cout << post_line;
        cout.flush();
        loop.set_timeout(on_draw, 0);
    };
    loop.set_timeout(on_draw, 0);
    cout << endl << endl << "\x1b[1A";
    cout.flush();
    loop.loop();
}