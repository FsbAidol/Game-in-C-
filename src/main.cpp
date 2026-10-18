#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>

#include <chrono>
#include <random>

#include "app.hpp"

namespace {

constexpr const char kRestore[] = "\x1b[0m\x1b[?25h\x1b[?1049l";

void restoreAndExit(int) {
    const ssize_t ignored = ::write(STDOUT_FILENO, kRestore, sizeof(kRestore) - 1);
    (void)ignored;
    _exit(0);
}

void printHelp(const char* program) {
    std::printf(
        "SNAKE · C++17 terminal edition\n"
        "\n"
        "Использование: %s [опции]\n"
        "\n"
        "  --size WxH        размер виртуального экрана (для браузерного режима)\n"
        "  --seed N          зерно генератора случайных чисел\n"
        "  --dump [title|game]  напечатать кадр обычным текстом и выйти\n"
        "  --ansi            с --dump: печатать полный ANSI-кадр с цветами\n"
        "  --dump-ticks N    сколько тиков сыграть перед --dump game (по умолчанию 14)\n"
        "  -h, --help        эта справка\n"
        "\n"
        "Управление:\n"
        "  WASD / стрелки    движение (работает и русская раскладка: ц ф ы в)\n"
        "  P                 пауза\n"
        "  R                 заново\n"
        "  Q                 выход\n",
        program);
}

bool parseSize(const std::string& text, int& width, int& height) {
    int w = 0;
    int h = 0;
    if (std::sscanf(text.c_str(), "%dx%d", &w, &h) != 2) return false;
    if (w < 40 || h < 18) return false;
    width = std::min(w, 300);
    height = std::min(h, 100);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    int width = 0;
    int height = 0;
    int dumpTicks = 14;
    bool ansi = false;
    std::string dump;
    std::uint32_t seed = static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto nextValue = [&](const std::string& option) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Не хватает значения для %s\n", option.c_str());
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return 0;
        } else if (arg == "--size") {
            if (!parseSize(nextValue(arg), width, height)) {
                std::fprintf(stderr, "Размер должен быть в формате WxH, не меньше 40x18\n");
                return 2;
            }
        } else if (arg.rfind("--size=", 0) == 0) {
            if (!parseSize(arg.substr(7), width, height)) {
                std::fprintf(stderr, "Размер должен быть в формате WxH, не меньше 40x18\n");
                return 2;
            }
        } else if (arg == "--seed") {
            seed = static_cast<std::uint32_t>(std::stoul(nextValue(arg)));
        } else if (arg.rfind("--seed=", 0) == 0) {
            seed = static_cast<std::uint32_t>(std::stoul(arg.substr(7)));
        } else if (arg == "--dump") {
            dump = "title";
            if (i + 1 < argc) {
                const std::string next = argv[i + 1];
                if (next == "title" || next == "game") {
                    dump = next;
                    ++i;
                }
            }
        } else if (arg.rfind("--dump=", 0) == 0) {
            dump = arg.substr(7);
        } else if (arg == "--ansi") {
            ansi = true;
        } else if (arg == "--dump-ticks") {
            dumpTicks = std::stoi(nextValue(arg));
        } else {
            std::fprintf(stderr, "Неизвестный аргумент: %s\n\n", arg.c_str());
            printHelp(argv[0]);
            return 2;
        }
    }

    if (!dump.empty() && dump != "title" && dump != "game") {
        std::fprintf(stderr, "--dump принимает только 'title' или 'game'\n");
        return 2;
    }

    App app(width, height, seed);

    if (!dump.empty()) {
        const std::string frame = dump == "game" ? app.dumpGame(dumpTicks, ansi) : app.dumpTitle();
        std::fputs(frame.c_str(), stdout);
        // ANSI-кадр печатаем без завершающего перевода строки, иначе терминал
        // прокрутится на строку и «съест» верхний ряд.
        if (!ansi) std::fputc('\n', stdout);
        return 0;
    }

    std::signal(SIGINT, restoreAndExit);
    std::signal(SIGTERM, restoreAndExit);
    std::signal(SIGHUP, restoreAndExit);

    return app.run();
}
