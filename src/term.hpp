// Крошечная обёртка над ANSI-терминалом.
//
// Два режима:
//   * "живой" терминал (TTY)      — размер берётся у терминала, клавиши читаются
//                                   без Enter, включён чередующийся экран;
//   * "тупой" терминал (браузер)  — те же escape-последовательности, но размер
//                                   приходит в argv, а клавиши — построчно в stdin.
#pragma once

#include <string>
#include <termios.h>

namespace term {

class Terminal {
public:
    Terminal(int fixedWidth, int fixedHeight);  // 0/0 => взять размер у TTY
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    void enter();
    void leave();

    int width() const { return width_; }
    int height() const { return height_; }
    bool interactive() const { return interactive_; }

    void refreshSize();
    void clear();
    void write(const std::string& s);
    void flush();

    // Неблокирующее чтение: true, если есть фрагмент ввода.
    bool readKey(std::string& key);
    void sleepMs(int ms);

private:
    void updateSize();

    int width_;
    int height_;
    bool interactive_ = false;
    bool entered_ = false;
    std::string out_;
    std::string lineBuffer_;

    struct termios original_{};
    bool hasOriginal_ = false;
    int originalFlags_ = -1;
};

}  // namespace term
