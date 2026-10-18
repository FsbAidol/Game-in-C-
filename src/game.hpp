// Snake — чистая игровая логика без ввода-вывода.
// Ядро не знает ничего про терминал, ANSI и таймеры: это позволяет
// тестировать правила игры отдельно от отрисовки.
#pragma once

#include <cstdint>
#include <deque>
#include <random>
#include <vector>

namespace snake {

struct Point {
    int x = 0;
    int y = 0;

    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Point& other) const { return !(*this == other); }
};

enum class Direction { Up, Down, Left, Right };

// Бонусное яблоко: появляется на время и даёт больше очков.
struct Bonus {
    Point pos{};
    int ticksLeft = 0;

    bool active() const { return ticksLeft > 0; }
};

// Что произошло за один игровой тик.
struct TickResult {
    bool ate = false;        // съедено обычное яблоко
    bool ateBonus = false;   // съедено золотое яблоко
    bool died = false;       // столкновение со стеной или с собой
    bool levelUp = false;    // вырос уровень
    bool bonusSpawned = false;
};

class Game {
public:
    static constexpr int kScorePerLevel = 5;
    static constexpr int kBonusEveryFoods = 4;
    static constexpr int kBonusScore = 5;
    static constexpr int kBonusTtlTicks = 75;

    // width/height задаются в клетках игрового поля.
    Game(int width, int height, std::uint32_t seed);

    // Начать заново на том же поле и с тем же генератором.
    void reset();

    // Поставить новое направление в очередь. Разворот на 180°
    // и повтор текущего направления игнорируются.
    void setDirection(Direction dir);

    // Продвинуть игру на один тик.
    TickResult tick();

    int width() const { return width_; }
    int height() const { return height_; }
    const std::deque<Point>& snake() const { return snake_; }
    Point food() const { return food_; }
    const Bonus& bonus() const { return bonus_; }
    int score() const { return score_; }
    int level() const { return level_; }
    int foodsEaten() const { return foodsEaten_; }
    bool alive() const { return alive_; }
    bool won() const { return won_; }
    Direction direction() const { return direction_; }

    // Помощники для тестов (и потенциальных бонусов).
    void setFood(Point p) { food_ = p; }
    void setBonus(Point p, int ttl);

private:
    Point randomFreeCell();
    void spawnFood();
    void maybeSpawnBonus();
    static bool opposite(Direction a, Direction b);
    static Point step(Point p, Direction d);

    int width_;
    int height_;
    std::mt19937 rng_;

    std::deque<Point> snake_;
    std::deque<Direction> pending_;  // буфер нажатий, чтобы быстрые повороты не терялись
    Direction direction_ = Direction::Right;
    Point food_{};
    Bonus bonus_{};
    int score_ = 0;
    int level_ = 1;
    int foodsEaten_ = 0;
    bool alive_ = true;
    bool won_ = false;
};

}  // namespace snake
