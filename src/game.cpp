#include "game.hpp"

#include <algorithm>
#include <cassert>

namespace snake {

Game::Game(int width, int height, std::uint32_t seed)
    : width_(width), height_(height), rng_(seed) {
    assert(width >= 10 && height >= 8);
    reset();
}

void Game::reset() {
    snake_.clear();
    pending_.clear();

    const int cx = width_ / 2;
    const int cy = height_ / 2;
    snake_.push_back({cx, cy});
    snake_.push_back({cx - 1, cy});
    snake_.push_back({cx - 2, cy});

    direction_ = Direction::Right;
    score_ = 0;
    level_ = 1;
    foodsEaten_ = 0;
    alive_ = true;
    won_ = false;
    bonus_ = {};
    spawnFood();
}

bool Game::opposite(Direction a, Direction b) {
    return (a == Direction::Up && b == Direction::Down) ||
           (a == Direction::Down && b == Direction::Up) ||
           (a == Direction::Left && b == Direction::Right) ||
           (a == Direction::Right && b == Direction::Left);
}

Point Game::step(Point p, Direction d) {
    switch (d) {
        case Direction::Up: --p.y; break;
        case Direction::Down: ++p.y; break;
        case Direction::Left: --p.x; break;
        case Direction::Right: ++p.x; break;
    }
    return p;
}

void Game::setDirection(Direction dir) {
    if (!alive_) return;
    const Direction last = pending_.empty() ? direction_ : pending_.back();
    if (dir == last || opposite(dir, last)) return;
    if (pending_.size() < 3) pending_.push_back(dir);
}

void Game::setBonus(Point p, int ttl) {
    bonus_.pos = p;
    bonus_.ticksLeft = std::max(0, ttl);
}

Point Game::randomFreeCell() {
    std::vector<Point> freeCells;
    freeCells.reserve(static_cast<std::size_t>(width_ * height_));

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Point p{x, y};
            const bool onSnake =
                std::find(snake_.begin(), snake_.end(), p) != snake_.end();
            const bool onFood = p == food_;
            const bool onBonus = bonus_.active() && p == bonus_.pos;
            if (!onSnake && !onFood && !onBonus) freeCells.push_back(p);
        }
    }

    if (freeCells.empty()) return Point{-1, -1};
    std::uniform_int_distribution<std::size_t> dist(0, freeCells.size() - 1);
    return freeCells[dist(rng_)];
}

void Game::spawnFood() {
    food_ = randomFreeCell();
}

void Game::maybeSpawnBonus() {
    if (bonus_.active()) return;
    if (foodsEaten_ == 0 || foodsEaten_ % kBonusEveryFoods != 0) return;

    bonus_.pos = randomFreeCell();
    if (bonus_.pos.x < 0) {
        bonus_ = {};
        return;
    }
    bonus_.ticksLeft = kBonusTtlTicks;
}

TickResult Game::tick() {
    TickResult result;
    if (!alive_) return result;

    if (!pending_.empty()) {
        direction_ = pending_.front();
        pending_.pop_front();
    }

    const Point head = snake_.front();
    const Point next = step(head, direction_);

    // Стены: удар о границу поля.
    if (next.x < 0 || next.x >= width_ || next.y < 0 || next.y >= height_) {
        alive_ = false;
        result.died = true;
        return result;
    }

    const bool grows = next == food_;
    const bool eatsBonus = bonus_.active() && next == bonus_.pos;

    // Хвост уйдёт с поля на этом же тике, если змейка не растёт,
    // поэтому в него врезаться можно.
    std::size_t solid = snake_.size();
    if (!grows && solid > 0) --solid;
    for (std::size_t i = 0; i < solid; ++i) {
        if (snake_[i] == next) {
            alive_ = false;
            result.died = true;
            return result;
        }
    }

    snake_.push_front(next);

    if (grows && food_.x >= 0) {
        ++score_;
        ++foodsEaten_;
        result.ate = true;

        const int newLevel = 1 + score_ / kScorePerLevel;
        if (newLevel != level_) {
            level_ = newLevel;
            result.levelUp = true;
        }
        spawnFood();
        maybeSpawnBonus();
        result.bonusSpawned = bonus_.active();

        // Поле заполнено целиком — это победа.
        if (snake_.size() == static_cast<std::size_t>(width_ * height_)) {
            won_ = true;
            alive_ = false;
            return result;
        }
    } else {
        snake_.pop_back();
    }

    if (eatsBonus) {
        score_ += kBonusScore;
        bonus_ = {};
        result.ateBonus = true;

        const int newLevel = 1 + score_ / kScorePerLevel;
        if (newLevel != level_) {
            level_ = newLevel;
            result.levelUp = true;
        }
    }

    if (bonus_.active()) {
        --bonus_.ticksLeft;
        if (bonus_.ticksLeft <= 0) bonus_ = {};
    }

    return result;
}

}  // namespace snake
