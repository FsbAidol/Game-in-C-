#include "term.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <thread>

namespace term {
namespace {

constexpr const char* kEnterScreen = "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
constexpr const char* kLeaveScreen = "\x1b[0m\x1b[?25h\x1b[?1049l";

void writeAll(int fd, const std::string& data) {
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        written += static_cast<std::size_t>(n);
    }
}

}  // namespace

Terminal::Terminal(int fixedWidth, int fixedHeight)
    : width_(fixedWidth), height_(fixedHeight) {
    interactive_ = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if (interactive_) updateSize();
    if (width_ <= 0) width_ = 80;
    if (height_ <= 0) height_ = 24;
}

Terminal::~Terminal() {
    if (entered_) leave();
}

void Terminal::updateSize() {
    struct winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        width_ = ws.ws_col;
        height_ = ws.ws_row;
    }
}

void Terminal::refreshSize() {
    if (interactive_) updateSize();
}

void Terminal::enter() {
    if (entered_) return;
    entered_ = true;

    if (interactive_) {
        if (tcgetattr(STDIN_FILENO, &original_) == 0) {
            hasOriginal_ = true;
            struct termios raw = original_;
            raw.c_lflag &= ~(ICANON | ECHO | ISIG);
            raw.c_iflag &= ~(ICRNL | IXON);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        }
        originalFlags_ = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (originalFlags_ >= 0) {
            fcntl(STDIN_FILENO, F_SETFL, originalFlags_ | O_NONBLOCK);
        }
    } else {
        originalFlags_ = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (originalFlags_ >= 0) {
            fcntl(STDIN_FILENO, F_SETFL, originalFlags_ | O_NONBLOCK);
        }
    }

    out_.clear();
    out_ += kEnterScreen;
    flush();
}

void Terminal::leave() {
    if (!entered_) return;
    out_.clear();
    out_ += kLeaveScreen;
    flush();

    if (interactive_ && hasOriginal_) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_);
    }
    if (originalFlags_ >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, originalFlags_);
    }
    entered_ = false;
}

void Terminal::clear() {
    out_ += "\x1b[2J\x1b[H";
}

void Terminal::write(const std::string& s) {
    out_ += s;
}

void Terminal::flush() {
    if (out_.empty()) return;
    writeAll(STDOUT_FILENO, out_);
    out_.clear();
}

bool Terminal::readKey(std::string& key) {
    if (interactive_) {
        char buffer[32];
        const ssize_t n = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            return false;
        }
        key.assign(buffer, static_cast<std::size_t>(n));
        return true;
    }

    // Построчный режим: команды приходят как "w\n", "enter\n" и т.п.
    char c = 0;
    for (;;) {
        const ssize_t n = ::read(STDIN_FILENO, &c, 1);
        if (n <= 0) break;
        lineBuffer_.push_back(c);
    }

    const std::size_t newline = lineBuffer_.find('\n');
    if (newline == std::string::npos) {
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
        return false;
    }

    key = lineBuffer_.substr(0, newline);
    lineBuffer_.erase(0, newline + 1);
    if (!key.empty() && key.back() == '\r') key.pop_back();
    return true;
}

void Terminal::sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace term
