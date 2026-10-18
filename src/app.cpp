#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "screen.hpp"

namespace {

using ui::Screen;

constexpr const char* kLogo[6] = {
    "███████╗███╗   ██╗ █████╗ ██╗  ██╗███████╗  ",
    "██╔════╝████╗  ██║██╔══██╗██║ ██╔╝██╔════╝  ",
    "███████╗██╔██╗ ██║███████║█████╔╝ █████╗    ",
    "╚════██║██║╚██╗██║██╔══██║██╔═██╗ ██╔══╝    ",
    "███████║██║ ╚████║██║  ██║██║  ██╗███████╗  ",
    "╚══════╝╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝  ",
};
constexpr int kLogoRows = 6;
constexpr int kLogoWidth = 44;

// Аркадный размер поля: одинаковый на титульном экране и в игре.
constexpr int kBoardWidth = 56;
constexpr int kBoardHeight = 26;

snake::Point nextHead(const snake::Game& game) {
    snake::Point p = game.snake().front();
    switch (game.direction()) {
        case snake::Direction::Up: --p.y; break;
        case snake::Direction::Down: ++p.y; break;
        case snake::Direction::Left: --p.x; break;
        case snake::Direction::Right: ++p.x; break;
    }
    return p;
}

}  // namespace

App::App(int fixedWidth, int fixedHeight, std::uint32_t seed)
    : term_(fixedWidth, fixedHeight), rng_(seed), seed_(seed) {
    titleStart_ = now_;
}

int App::tickMsForLevel(int level) {
    const int speed = 150 - (std::max(1, level) - 1) * 12;
    return std::max(70, speed);
}

ui::Layout App::layout() const {
    if (game_ && state_ != State::Title) {
        return ui::computeLayout(term_.width(), term_.height(), game_->width(), game_->height(),
                                 true);
    }
    return ui::computeLayout(term_.width(), term_.height(), kBoardWidth, kBoardHeight, true);
}

// ---------------------------------------------------------------------------
// Игровой цикл
// ---------------------------------------------------------------------------

int App::run() {
    term_.enter();
    term_.clear();
    term_.flush();

    setupDemo();
    titleStart_ = 0.0;

    auto last = std::chrono::steady_clock::now();
    while (!quit_) {
        std::string chunk;
        while (term_.readKey(chunk)) {
            handleChunk(chunk);
            if (quit_) break;
        }
        if (quit_) break;

        const auto frameNow = std::chrono::steady_clock::now();
        const double dt =
            std::chrono::duration<double>(frameNow - last).count();
        last = frameNow;
        now_ += dt;

        if (state_ == State::Title) {
            updateDemo(dt);
        } else if (state_ == State::Playing && game_) {
            tickAcc_ += dt;
            const double step = tickMsForLevel(game_->level()) / 1000.0;
            int guard = 0;
            while (tickAcc_ >= step && guard++ < 8) {
                tickAcc_ -= step;
                tickGame();
                if (!game_->alive()) break;
            }
        } else {
            tickAcc_ = 0.0;
        }

        if (toastLeft_ > 0.0) {
            toastLeft_ = std::max(0.0, toastLeft_ - dt);
            if (toastLeft_ == 0.0) dirty_ = true;
        }

        if (now_ - lastResizeCheck_ > 0.4) {
            lastResizeCheck_ = now_;
            const int oldW = term_.width();
            const int oldH = term_.height();
            term_.refreshSize();
            if (term_.width() != oldW || term_.height() != oldH) {
                dirty_ = true;
                if (state_ == State::Title) setupDemo();
            }
        }

        const double interval = state_ == State::Title
                                    ? ((now_ - titleStart_ < 2.6) ? 0.07 : 0.13)
                                    : 0.03;
        const bool animated = state_ == State::Title || toastLeft_ > 0.0;
        if ((dirty_ || animated) && now_ - lastRender_ >= interval) {
            term_.write(buildScreen().ansi());
            term_.flush();
            lastRender_ = now_;
            dirty_ = false;
        }

        term_.sleepMs(state_ == State::Title ? 6 : 8);
    }

    term_.leave();
    return 0;
}

void App::startGame() {
    const auto l = layout();
    const int w = std::max(12, l.fieldW - 2);
    const int h = std::max(10, l.fieldH - 2);
    const std::uint32_t seed = static_cast<std::uint32_t>(rng_());
    game_ = std::make_unique<snake::Game>(w, h, seed);
    state_ = State::Playing;
    tickAcc_ = 0.0;
    toastLeft_ = 0.0;
    won_ = false;
    newRecord_ = false;
    dirty_ = true;
}

void App::tickGame() {
    if (!game_) return;
    const auto result = game_->tick();
    if (result.levelUp) {
        toastLeft_ = 1.3;
        toastLevel_ = game_->level();
    }
    if (!game_->alive()) {
        lastScore_ = game_->score();
        won_ = game_->won();
        newRecord_ = lastScore_ > best_;
        best_ = std::max(best_, lastScore_);
        state_ = State::GameOver;
    }
    dirty_ = true;
}

// ---------------------------------------------------------------------------
// Ввод
// ---------------------------------------------------------------------------

void App::handleChunk(const std::string& chunk) {
    std::size_t i = 0;
    while (i < chunk.size()) {
        const unsigned char c = static_cast<unsigned char>(chunk[i]);

        if (c == 0x1b) {
            if (i + 2 < chunk.size() && chunk[i + 1] == '[') {
                switch (chunk[i + 2]) {
                    case 'A': handleCommand("up"); break;
                    case 'B': handleCommand("down"); break;
                    case 'C': handleCommand("right"); break;
                    case 'D': handleCommand("left"); break;
                    default: break;
                }
                i += 3;
            } else {
                ++i;
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            handleCommand("enter");
            ++i;
            continue;
        }

        if (c == 0x03) {  // Ctrl+C
            handleCommand("quit");
            ++i;
            continue;
        }

        std::size_t len = 1;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (i + len > chunk.size()) len = 1;
        const std::string token = chunk.substr(i, len);
        i += len;

        if (token == "w" || token == "W" || token == "ц" || token == "Ц" || token == "up") {
            handleCommand("up");
        } else if (token == "a" || token == "A" || token == "ф" || token == "Ф" ||
                   token == "left") {
            handleCommand("left");
        } else if (token == "s" || token == "S" || token == "ы" || token == "Ы" ||
                   token == "down") {
            handleCommand("down");
        } else if (token == "d" || token == "D" || token == "в" || token == "В" ||
                   token == "right") {
            handleCommand("right");
        } else if (token == "p" || token == "P" || token == "п" || token == "П" ||
                   token == "pause") {
            handleCommand("pause");
        } else if (token == "r" || token == "R" || token == "к" || token == "К" ||
                   token == "restart") {
            handleCommand("restart");
        } else if (token == "q" || token == "Q" || token == "й" || token == "Й" ||
                   token == "quit" || token == "exit") {
            handleCommand("quit");
        } else if (token == "enter" || token == "start" || token == "space") {
            handleCommand("enter");
        }
    }
}

void App::handleCommand(const std::string& command) {
    if (command == "quit") {
        quit_ = true;
        return;
    }
    if (command == "restart") {
        startGame();
        return;
    }

    if (state_ == State::Title) {
        if (command == "enter") startGame();
        return;
    }

    if (state_ == State::GameOver) {
        if (command == "enter") startGame();
        return;
    }

    if (command == "pause") {
        if (state_ == State::Playing) {
            state_ = State::Paused;
        } else if (state_ == State::Paused) {
            state_ = State::Playing;
            tickAcc_ = 0.0;
        }
        dirty_ = true;
        return;
    }

    if (state_ == State::Paused) {
        if (command == "enter") {
            state_ = State::Playing;
            tickAcc_ = 0.0;
            dirty_ = true;
        }
        return;
    }

    if (!game_ || state_ != State::Playing) return;
    if (command == "up") game_->setDirection(snake::Direction::Up);
    if (command == "down") game_->setDirection(snake::Direction::Down);
    if (command == "left") game_->setDirection(snake::Direction::Left);
    if (command == "right") game_->setDirection(snake::Direction::Right);
}

// ---------------------------------------------------------------------------
// Демо-змея на титульном экране
// ---------------------------------------------------------------------------

void App::setupDemo() {
    demo_.clear();
    demoApples_.clear();
    demoDir_ = {1, 0};
    demoTimer_ = 0.0;

    const auto l = layout();
    const int w = l.innerW();
    const int h = l.innerH();
    if (w < 12 || h < 7) return;

    const int y = std::max(2, h - 4);
    const int x = std::max(8, w / 3);
    for (int i = 0; i < 10; ++i) demo_.push_back({x - i, y});

    std::uniform_int_distribution<int> dx(1, std::max(1, w - 2));
    std::uniform_int_distribution<int> dy(1, std::max(1, h - 2));
    for (int attempt = 0; attempt < 120 && static_cast<int>(demoApples_.size()) < 3; ++attempt) {
        snake::Point p{dx(rng_), dy(rng_)};
        if (std::find(demo_.begin(), demo_.end(), p) != demo_.end()) continue;
        if (std::find(demoApples_.begin(), demoApples_.end(), p) != demoApples_.end()) continue;
        demoApples_.push_back(p);
    }
}

void App::updateDemo(double dt) {
    constexpr double stepTime = 0.16;
    demoTimer_ += dt;
    if (demoTimer_ < stepTime) return;
    demoTimer_ = std::fmod(demoTimer_, stepTime);

    const auto l = layout();
    const int w = l.innerW();
    const int h = l.innerH();
    if (w < 12 || h < 7) return;
    if (demo_.empty() || demo_.front().x >= w || demo_.front().y >= h) {
        setupDemo();
        return;
    }

    const snake::Point head = demo_.front();
    snake::Point target = head;
    int bestDist = 1 << 30;
    for (const auto& apple : demoApples_) {
        const int dist = std::abs(apple.x - head.x) + std::abs(apple.y - head.y);
        if (dist < bestDist) {
            bestDist = dist;
            target = apple;
        }
    }

    const snake::Point dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    std::vector<std::pair<int, snake::Point>> candidates;
    for (const auto& d : dirs) {
        const snake::Point next{head.x + d.x, head.y + d.y};
        if (next.x < 1 || next.x >= w - 1 || next.y < 1 || next.y >= h - 1) continue;
        bool hitsBody = false;
        for (std::size_t i = 0; i + 1 < demo_.size(); ++i) {
            if (demo_[i] == next) hitsBody = true;
        }
        if (hitsBody) continue;
        candidates.emplace_back(std::abs(target.x - next.x) + std::abs(target.y - next.y), d);
    }
    if (candidates.empty()) {
        setupDemo();
        return;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    snake::Point chosen = candidates.front().second;
    std::uniform_int_distribution<int> chance(0, 99);
    if (chance(rng_) >= 68 && candidates.size() > 1) {
        std::uniform_int_distribution<std::size_t> pick(1, candidates.size() - 1);
        chosen = candidates[pick(rng_)].second;
    }
    if (chosen.x == -demoDir_.x && chosen.y == -demoDir_.y && candidates.size() > 1) {
        chosen = candidates[1].second;
    }
    demoDir_ = chosen;

    const snake::Point next{head.x + chosen.x, head.y + chosen.y};
    demo_.push_front(next);

    bool ate = false;
    for (auto it = demoApples_.begin(); it != demoApples_.end(); ++it) {
        if (*it == next) {
            demoApples_.erase(it);
            ate = true;
            break;
        }
    }
    if (!ate || demo_.size() > 18) demo_.pop_back();

    if (ate) {
        std::uniform_int_distribution<int> dx(1, std::max(1, w - 2));
        std::uniform_int_distribution<int> dy(1, std::max(1, h - 2));
        for (int attempt = 0; attempt < 120; ++attempt) {
            snake::Point p{dx(rng_), dy(rng_)};
            if (std::find(demo_.begin(), demo_.end(), p) != demo_.end()) continue;
            if (std::find(demoApples_.begin(), demoApples_.end(), p) != demoApples_.end()) continue;
            demoApples_.push_back(p);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Отрисовка
// ---------------------------------------------------------------------------

void App::drawRight(Screen& s, int row, int right, const std::string& text, int fg, int bg) {
    const int len = static_cast<int>(ui::utf8Len(text));
    s.text(row, right - len + 1, text, fg, bg);
}

ui::Screen App::buildScreen() {
    ui::Screen screen(term_.width(), term_.height());
    screen.clear(-1, ui::pal::bgDeep);

    const auto l = layout();
    drawHeader(screen, l);
    drawFooter(screen, l);
    drawBoard(screen, l);

    if (state_ == State::Title) {
        drawDemo(screen, l);
        drawTitle(screen, l);
    } else {
        drawGame(screen, l);
        drawToast(screen, l);
        if (state_ == State::Paused) drawPause(screen, l);
        if (state_ == State::GameOver) drawGameOver(screen, l);
    }
    drawPanel(screen, l);
    return screen;
}

void App::drawHeader(Screen& s, const ui::Layout& l) {
    s.text(1, 2, "●", ui::pal::accent, ui::pal::bgDeep);
    s.text(1, 4, "S N A K E", ui::pal::textBright, ui::pal::bgDeep);

    const int leftEnd = 4 + static_cast<int>(ui::utf8Len("S N A K E")) - 1;

    std::string right;
    const int rightFg = ui::pal::dim;
    if (game_ && state_ != State::Title) {
        right = "очки " + std::to_string(game_->score()) + "   ур. " +
                std::to_string(game_->level()) + "   рекорд " +
                std::to_string(std::max(best_, game_->score()));
    } else {
        right = "демо · ANSI-терминал";
    }

    const int rightLen = static_cast<int>(ui::utf8Len(right));
    const int rightStart = l.termW - 2 - rightLen + 1;
    if (rightStart > leftEnd + 3) {
        if (l.termW >= 60 && state_ == State::Title) {
            s.text(1, leftEnd + 3, "· C++17", ui::pal::dim, ui::pal::bgDeep);
        }
        s.text(1, rightStart, right, rightFg, ui::pal::bgDeep);
    } else if (l.termW >= 52) {
        // Мало места — оставляем только счёт.
        std::string compact;
        if (game_ && state_ != State::Title) {
            compact = std::to_string(game_->score()) + " · ур " + std::to_string(game_->level()) +
                      " · рек " + std::to_string(std::max(best_, game_->score()));
        }
        const int len = static_cast<int>(ui::utf8Len(compact));
        const int start = l.termW - 2 - len + 1;
        if (!compact.empty() && start > leftEnd + 2) {
            s.text(1, start, compact, ui::pal::dim, ui::pal::bgDeep);
        }
    }

    for (int x = 2; x <= l.termW - 1; ++x) {
        s.put(2, x, "─", ui::pal::borderDim, ui::pal::bgDeep);
    }
}

void App::drawFooter(Screen& s, const ui::Layout& l) {
    std::string hints;
    if (l.termW >= 78) {
        hints = "WASD / ←↑↓→ — движение   ·   P — пауза   ·   R — заново   ·   Q — выход";
    } else if (l.termW >= 52) {
        hints = "WASD / стрелки · P — пауза · Q — выход";
    } else {
        hints = "WASD · P пауза · Q выход";
    }
    if (state_ == State::GameOver) {
        hints = "R — заново   ·   Q — выход";
    } else if (state_ == State::Paused) {
        hints = "P — продолжить   ·   Q — выход";
    } else if (state_ == State::Title) {
        hints = "ENTER — начать игру   ·   Q — выход";
    }
    s.textCentered(l.termH, 1, l.termW, hints, ui::pal::dim, ui::pal::bgDeep);
}

void App::drawBoard(Screen& s, const ui::Layout& l) {
    s.fill(l.fieldY + 1, l.fieldX + 1, l.fieldH - 2, l.fieldW - 2, -1, ui::pal::bgBoard);

    int borderColor = ui::pal::border;
    if (state_ == State::Paused) borderColor = ui::pal::warn;
    if (state_ == State::GameOver) borderColor = ui::pal::danger;
    s.frame(l.fieldY, l.fieldX, l.fieldH, l.fieldW, borderColor, ui::pal::bgBoard);
}

void App::drawDemo(Screen& s, const ui::Layout& l) {
    if (state_ != State::Title) return;
    const int ox = l.innerX();
    const int oy = l.innerY();

    for (const auto& apple : demoApples_) {
        s.put(oy + apple.y, ox + apple.x, "●", ui::pal::apple, ui::pal::bgBoard);
    }

    int index = 0;
    for (const auto& part : demo_) {
        if (index == 0) {
            s.put(oy + part.y, ox + part.x, "▶", ui::pal::bgDeep, ui::pal::snakeHead);
        } else {
            s.put(oy + part.y, ox + part.x, " ",
                  -1, index % 2 == 0 ? ui::pal::snakeBody : ui::pal::snakeBodyAlt);
        }
        ++index;
    }
}

void App::drawTitle(Screen& s, const ui::Layout& l) {
    const int innerW = l.innerW();
    const int innerH = l.innerH();
    if (innerW < 26 || innerH < 7) return;

    const bool fancy = innerW >= 52 && innerH >= 16;
    int boxW = fancy ? 48 : std::min(innerW - 2, 40);
    int boxH = fancy ? 14 : 9;
    boxW = std::min(boxW, innerW - 2);
    boxH = std::min(boxH, innerH - 2);
    if (boxW < 16 || boxH < 6) return;

    const int bx = l.innerX() + (innerW - boxW) / 2;
    const int by = l.innerY() + (innerH - boxH) / 2;

    s.fill(by, bx, boxH, boxW, -1, ui::pal::bgBox);
    s.frame(by, bx, boxH, boxW, ui::pal::border, ui::pal::bgBox);

    const bool blink = std::fmod(now_, 1.2) < 0.72;

    if (fancy) {
        drawLogo(s, by + 2, bx + 1, bx + boxW - 2, true);
        s.textCentered(by + 9, bx + 1, bx + boxW - 2, "C + +   ·   Т Е Р М И Н А Л",
                       ui::pal::dim, ui::pal::bgBox);
        s.textCentered(by + 11, bx + 1, bx + boxW - 2, "▶  ENTER — НАЧАТЬ ИГРУ  ◀",
                       blink ? ui::pal::accent : ui::pal::accentSoft, ui::pal::bgBox);
        s.textCentered(by + 12, bx + 1, bx + boxW - 2, "или нажмите стрелку", ui::pal::dim,
                       ui::pal::bgBox);
    } else {
        const int titleRow = by + 2;
        s.textCentered(titleRow, bx + 1, bx + boxW - 2, "S N A K E", ui::pal::accent,
                       ui::pal::bgBox);
        s.textCentered(titleRow + 2, bx + 1, bx + boxW - 2, "C++ · ЗМЕЙКА", ui::pal::dim,
                       ui::pal::bgBox);
        s.textCentered(titleRow + 4, bx + 1, bx + boxW - 2,
                       blink ? "ENTER — ИГРАТЬ" : "· · ·", ui::pal::accent, ui::pal::bgBox);
    }
}

void App::drawLogo(Screen& s, int top, int left, int right, bool reveal) {
    const int width = right - left + 1;
    if (width < kLogoWidth) return;
    const int x = left + (width - kLogoWidth) / 2;

    const double elapsed = now_ - titleStart_;
    const int revealCols = reveal ? static_cast<int>(elapsed * 30.0) : 1000;
    const int rowColors[6] = {46, 82, 118, 154, 190, 226};

    for (int r = 0; r < kLogoRows; ++r) {
        const auto glyphs = ui::utf8Glyphs(kLogo[r]);
        for (std::size_t i = 0; i < glyphs.size() && static_cast<int>(i) < kLogoWidth; ++i) {
            if (static_cast<int>(i) >= revealCols) continue;
            s.put(top + r, x + static_cast<int>(i), glyphs[i], rowColors[r], ui::pal::bgBox);
        }
    }
}

void App::drawGame(Screen& s, const ui::Layout& l) {
    if (!game_) return;
    const int ox = l.innerX();
    const int oy = l.innerY();
    const int gw = game_->width();
    const int gh = game_->height();
    const bool dead = !game_->alive();

    auto cell = [&](const snake::Point& p) -> std::pair<int, int> {
        return {oy + p.y, ox + p.x};
    };

    const snake::Point food = game_->food();
    if (food.x >= 0 && food.x < gw && food.y >= 0 && food.y < gh) {
        const auto [row, col] = cell(food);
        s.put(row, col, "●", ui::pal::apple, ui::pal::bgBoard);
    }

    if (game_->bonus().active()) {
        const auto [row, col] = cell(game_->bonus().pos);
        const bool blink = game_->bonus().ticksLeft <= 18 && std::fmod(now_, 0.4) < 0.2;
        s.put(row, col, "✦", blink ? ui::pal::dim : ui::pal::gold, ui::pal::bgBoard);
    }

    std::string headGlyph = "▶";
    switch (game_->direction()) {
        case snake::Direction::Up: headGlyph = "▲"; break;
        case snake::Direction::Down: headGlyph = "▼"; break;
        case snake::Direction::Left: headGlyph = "◀"; break;
        case snake::Direction::Right: headGlyph = "▶"; break;
    }
    if (dead) headGlyph = "×";

    int index = 0;
    for (const auto& part : game_->snake()) {
        if (part.x < 0 || part.x >= gw || part.y < 0 || part.y >= gh) {
            ++index;
            continue;
        }
        const auto [row, col] = cell(part);
        if (index == 0) {
            s.put(row, col, headGlyph, ui::pal::textBright,
                  dead ? 52 : ui::pal::snakeHead);
        } else {
            const int bg = dead ? 52
                                : (index % 2 == 0 ? ui::pal::snakeBody : ui::pal::snakeBodyAlt);
            s.put(row, col, " ", -1, bg);
        }
        ++index;
    }
}

void App::drawSeparator(Screen& s, int row, int x, int width) {
    s.put(row, x, "├", ui::pal::border, ui::pal::bgPanel);
    for (int i = 1; i < width - 1; ++i) {
        s.put(row, x + i, "─", ui::pal::border, ui::pal::bgPanel);
    }
    s.put(row, x + width - 1, "┤", ui::pal::border, ui::pal::bgPanel);
}

void App::drawPanel(Screen& s, const ui::Layout& l) {
    if (!l.hasPanel) return;
    const int x = l.panelX;
    const int y = l.panelY;
    const int w = l.panelW;
    const int h = l.panelH;

    s.fill(y, x, h, w, -1, ui::pal::bgPanel);
    s.frame(y, x, h, w, ui::pal::border, ui::pal::bgPanel);

    s.textCentered(y + 1, x + 1, x + w - 2, "S N A K E", ui::pal::accent, ui::pal::bgPanel);
    drawSeparator(s, y + 2, x, w);

    const int score = game_ ? game_->score() : 0;
    const int level = game_ ? game_->level() : 1;
    const int foods = game_ ? game_->foodsEaten() : 0;
    const int best = std::max(best_, score);

    s.text(y + 3, x + 2, "ОЧКИ", ui::pal::dim, ui::pal::bgPanel);
    s.text(y + 3, x + 17, "УРОВЕНЬ", ui::pal::dim, ui::pal::bgPanel);
    s.text(y + 4, x + 2, std::to_string(score), ui::pal::accent, ui::pal::bgPanel);
    s.text(y + 4, x + 17, std::to_string(level), ui::pal::textBright, ui::pal::bgPanel);

    s.text(y + 5, x + 2, "РЕКОРД", ui::pal::dim, ui::pal::bgPanel);
    s.text(y + 5, x + 17, "ЯБЛОКИ", ui::pal::dim, ui::pal::bgPanel);
    s.text(y + 6, x + 2, std::to_string(best),
           best > 0 ? ui::pal::gold : ui::pal::textBright, ui::pal::bgPanel);
    s.text(y + 6, x + 17, std::to_string(foods), ui::pal::apple, ui::pal::bgPanel);

    drawSeparator(s, y + 7, x, w);

    s.text(y + 8, x + 2, "БОНУС", ui::pal::dim, ui::pal::bgPanel);
    const int barW = 14;
    const bool bonus = game_ && game_->bonus().active();
    const int ticksLeft = bonus ? game_->bonus().ticksLeft : 0;
    const int filled = bonus ? std::max(
                                   0, std::min(barW, static_cast<int>(
                                                         std::lround(barW * ticksLeft /
                                                                     static_cast<double>(
                                                                         snake::Game::kBonusTtlTicks)))))
                             : 0;
    int barColor = ui::pal::accent;
    if (bonus && ticksLeft < 25) barColor = ui::pal::warn;
    if (bonus && ticksLeft < 10) barColor = ui::pal::danger;
    for (int i = 0; i < barW; ++i) {
        s.put(y + 9, x + 3 + i, "█", i < filled ? barColor : 238, ui::pal::bgPanel);
    }
    if (bonus) {
        char buf[32];
        const double seconds = ticksLeft * tickMsForLevel(level) / 1000.0;
        std::snprintf(buf, sizeof(buf), "%.0f с", std::ceil(seconds));
        s.text(y + 9, x + 19, buf, ui::pal::gold, ui::pal::bgPanel);
    } else {
        s.text(y + 9, x + 19, "—", ui::pal::dim, ui::pal::bgPanel);
    }

    drawSeparator(s, y + 10, x, w);

    s.text(y + 11, x + 2, "УПРАВЛЕНИЕ", ui::pal::dim, ui::pal::bgPanel);
    s.text(y + 12, x + 2, "WASD / ←↑↓→", ui::pal::text, ui::pal::bgPanel);
    s.text(y + 12, x + 16, "движение", ui::pal::dim, ui::pal::bgPanel);
    s.text(y + 13, x + 2, "P", ui::pal::accent, ui::pal::bgPanel);
    s.text(y + 13, x + 4, "— пауза", ui::pal::text, ui::pal::bgPanel);
    s.text(y + 14, x + 2, "R", ui::pal::accent, ui::pal::bgPanel);
    s.text(y + 14, x + 4, "— заново", ui::pal::text, ui::pal::bgPanel);
    s.text(y + 15, x + 2, "Q", ui::pal::accent, ui::pal::bgPanel);
    s.text(y + 15, x + 4, "— выход", ui::pal::text, ui::pal::bgPanel);

    if (h >= 22) {
        drawSeparator(s, y + 17, x, w);
        s.text(y + 18, x + 2, "ЗОЛОТОЕ ЯБЛОКО", ui::pal::dim, ui::pal::bgPanel);
        s.text(y + 19, x + 2, "✦ +5", ui::pal::gold, ui::pal::bgPanel);
        s.text(y + 19, x + 7, "к очкам", ui::pal::text, ui::pal::bgPanel);
        s.text(y + 20, x + 2, "раз в 4 яблока", ui::pal::dim, ui::pal::bgPanel);
    }
    if (h >= 26) {
        s.text(y + h - 2, x + 2, "C++17 · ANSI", 241, ui::pal::bgPanel);
    }
}

void App::drawPause(Screen& s, const ui::Layout& l) {
    const int innerW = l.innerW();
    const int innerH = l.innerH();
    const int boxW = std::min(innerW - 2, 30);
    const int boxH = 6;
    if (boxW < 16 || innerH < boxH + 2) return;
    const int bx = l.innerX() + (innerW - boxW) / 2;
    const int by = l.innerY() + (innerH - boxH) / 2;

    s.fill(by, bx, boxH, boxW, -1, ui::pal::bgBox);
    s.frame(by, bx, boxH, boxW, ui::pal::warn, ui::pal::bgBox);
    s.textCentered(by + 2, bx + 1, bx + boxW - 2, "ПАУЗА", ui::pal::warn, ui::pal::bgBox);
    s.textCentered(by + 4, bx + 1, bx + boxW - 2, "P — продолжить", ui::pal::dim,
                   ui::pal::bgBox);
}

void App::drawGameOver(Screen& s, const ui::Layout& l) {
    const int innerW = l.innerW();
    const int innerH = l.innerH();
    const int boxW = std::min(innerW - 2, 40);
    const int boxH = 9;
    if (boxW < 20 || innerH < boxH + 2) return;
    const int bx = l.innerX() + (innerW - boxW) / 2;
    const int by = l.innerY() + (innerH - boxH) / 2;

    const int accentColor = won_ ? ui::pal::gold : ui::pal::danger;
    s.fill(by, bx, boxH, boxW, -1, ui::pal::bgBox);
    s.frame(by, bx, boxH, boxW, accentColor, ui::pal::bgBox);

    s.textCentered(by + 2, bx + 1, bx + boxW - 2, won_ ? "ПОБЕДА!" : "ИГРА ОКОНЧЕНА",
                   accentColor, ui::pal::bgBox);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "ОЧКИ %d   ·   РЕКОРД %d", lastScore_, best_);
    s.textCentered(by + 4, bx + 1, bx + boxW - 2, buf, ui::pal::textBright, ui::pal::bgBox);

    if (newRecord_) {
        s.textCentered(by + 5, bx + 1, bx + boxW - 2, "★ НОВЫЙ РЕКОРД ★", ui::pal::gold,
                       ui::pal::bgBox);
    }

    s.textCentered(by + 7, bx + 1, bx + boxW - 2, "R — заново      Q — выход", ui::pal::dim,
                   ui::pal::bgBox);
}

void App::drawToast(Screen& s, const ui::Layout& l) {
    if (!game_ || toastLeft_ <= 0.0) return;
    const std::string text = "УРОВЕНЬ " + std::to_string(toastLevel_);
    const int len = static_cast<int>(ui::utf8Len(text));
    const int pillW = len + 4;
    if (pillW >= l.innerW() - 2) return;
    const int x = l.innerX() + (l.innerW() - pillW) / 2;
    const int y = l.innerY();
    s.fill(y, x, 1, pillW, -1, ui::pal::bgPanel);
    s.textCentered(y, x, x + pillW - 1, text, ui::pal::gold, ui::pal::bgPanel);
}

// ---------------------------------------------------------------------------
// Снимки экрана для отладки
// ---------------------------------------------------------------------------

std::string App::dumpTitle() {
    state_ = State::Title;
    now_ = 10.0;
    titleStart_ = 0.0;
    setupDemo();
    return buildScreen().plain();
}

std::string App::dumpGame(int ticks, bool ansi) {
    startGame();
    now_ = 10.0;

    // Небольшой сценарий, чтобы на кадре получилась изогнутая змейка:
    // несколько поворотов и яблоки по пути.
    struct Segment {
        snake::Direction dir;
        int steps;
    };
    const Segment path[] = {
        {snake::Direction::Right, 6}, {snake::Direction::Up, 4}, {snake::Direction::Left, 5},
        {snake::Direction::Down, 3},  {snake::Direction::Right, 2},
    };

    int total = 0;
    for (const auto& segment : path) {
        if (total >= ticks || !game_ || !game_->alive()) break;
        game_->setDirection(segment.dir);
        for (int i = 0; i < segment.steps && total < ticks; ++i, ++total) {
            if (total < 6) game_->setFood(nextHead(*game_));
            tickGame();
            if (!game_->alive()) break;
        }
    }
    while (total < ticks && game_ && game_->alive()) {
        tickGame();
        ++total;
    }

    toastLeft_ = 0.0;  // на «фотографии» плашка уровня только мешает
    ui::Screen screen = buildScreen();
    return ansi ? screen.ansi() : screen.plain();
}
