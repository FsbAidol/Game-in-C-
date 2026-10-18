#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "game.hpp"
#include "screen.hpp"
#include "term.hpp"

// Приложение целиком: состояния, ввод, анимация и отрисовка.
class App {
public:
    App(int fixedWidth, int fixedHeight, std::uint32_t seed);

    int run();

    // Текстовые «снимки» экрана — для отладки и тестов (без ANSI).
    std::string dumpTitle();
    std::string dumpGame(int ticks, bool ansi = false);

private:
    enum class State { Title, Playing, Paused, GameOver };

    void startGame();
    void handleChunk(const std::string& chunk);
    void handleCommand(const std::string& command);
    void tickGame();

    void setupDemo();
    void updateDemo(double dt);

    ui::Layout layout() const;
    ui::Screen buildScreen();
    void drawHeader(ui::Screen& s, const ui::Layout& l);
    void drawFooter(ui::Screen& s, const ui::Layout& l);
    void drawBoard(ui::Screen& s, const ui::Layout& l);
    void drawDemo(ui::Screen& s, const ui::Layout& l);
    void drawTitle(ui::Screen& s, const ui::Layout& l);
    void drawGame(ui::Screen& s, const ui::Layout& l);
    void drawPanel(ui::Screen& s, const ui::Layout& l);
    void drawPause(ui::Screen& s, const ui::Layout& l);
    void drawGameOver(ui::Screen& s, const ui::Layout& l);
    void drawToast(ui::Screen& s, const ui::Layout& l);
    void drawLogo(ui::Screen& s, int top, int left, int right, bool reveal);
    void drawSeparator(ui::Screen& s, int row, int x, int width);

    static int tickMsForLevel(int level);
    static void drawRight(ui::Screen& s, int row, int right, const std::string& text, int fg,
                          int bg);

    term::Terminal term_;
    std::mt19937 rng_;
    std::uint32_t seed_;

    State state_ = State::Title;
    bool quit_ = false;
    std::unique_ptr<snake::Game> game_;

    int best_ = 0;
    int lastScore_ = 0;
    bool newRecord_ = false;
    bool won_ = false;

    double now_ = 0.0;
    double tickAcc_ = 0.0;
    double titleStart_ = 0.0;
    double toastLeft_ = 0.0;
    int toastLevel_ = 1;
    double lastRender_ = -1.0;
    double lastResizeCheck_ = -1.0;
    bool dirty_ = true;

    // Демо-змея на титульном экране (координаты внутри поля).
    std::deque<snake::Point> demo_;
    snake::Point demoDir_{1, 0};
    std::vector<snake::Point> demoApples_;
    double demoTimer_ = 0.0;
};
