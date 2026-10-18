// Тесты игрового ядра — без терминала, без таймеров.
#include <algorithm>
#include <cstdio>
#include <string>

#include "game.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
    if (condition) {
        std::printf("ok   %s\n", name.c_str());
    } else {
        ++failures;
        std::printf("FAIL %s\n", name.c_str());
    }
}

snake::Point ahead(const snake::Game& game) {
    snake::Point p = game.snake().front();
    switch (game.direction()) {
        case snake::Direction::Up: --p.y; break;
        case snake::Direction::Down: ++p.y; break;
        case snake::Direction::Left: --p.x; break;
        case snake::Direction::Right: ++p.x; break;
    }
    return p;
}

void feed(snake::Game& game, int count) {
    for (int i = 0; i < count; ++i) {
        game.setFood(ahead(game));
        game.tick();
    }
}

void testInitialState() {
    snake::Game game(24, 16, 7);
    check(game.snake().size() == 3, "стартовая длина змейки — 3");
    check(game.alive(), "новая игра активна");
    check(game.score() == 0 && game.level() == 1, "счёт 0, уровень 1");
    check(game.food().x >= 0 && game.food().y >= 0, "яблоко появилось");
    check(std::find(game.snake().begin(), game.snake().end(), game.food()) == game.snake().end(),
          "яблоко не лежит на змейке");
    check(game.direction() == snake::Direction::Right, "начальное направление — вправо");
}

void testMovement() {
    snake::Game game(24, 16, 3);
    const auto head = game.snake().front();
    const auto result = game.tick();
    check(!result.died, "змейка жива после обычного хода");
    check(game.snake().front() == snake::Point{head.x + 1, head.y}, "голова сдвинулась вправо");
    check(game.snake().size() == 3, "длина не изменилась");
}

void testNoReverse() {
    snake::Game game(24, 16, 3);
    game.setDirection(snake::Direction::Left);
    game.tick();
    check(game.direction() == snake::Direction::Right, "разворот на 180° игнорируется");

    game.setDirection(snake::Direction::Up);
    game.setDirection(snake::Direction::Down);
    const auto head = game.snake().front();
    game.tick();
    check(game.snake().front() == snake::Point{head.x, head.y - 1},
          "второй поворот в очередь не попадает");
}

void testQueuedTurns() {
    snake::Game game(24, 16, 3);
    const auto start = game.snake().front();
    game.setDirection(snake::Direction::Up);
    game.setDirection(snake::Direction::Left);
    game.tick();
    check(game.snake().front() == snake::Point{start.x, start.y - 1}, "первый поворот сработал");
    game.tick();
    check(game.snake().front() == snake::Point{start.x - 1, start.y - 1},
          "второй поворот сработал на следующем тике");
}

void testFoodAndLevels() {
    snake::Game game(30, 16, 11);
    feed(game, 1);
    check(game.snake().size() == 4 && game.score() == 1, "яблоко растит змейку и даёт очко");
    check(game.level() == 1, "после первого яблока уровень ещё первый");

    feed(game, 4);
    check(game.level() == 2, "пять яблок — второй уровень");
    check(game.score() == 5, "счёт за пять яблок — 5");
}

void testWallCollision() {
    snake::Game game(20, 12, 5);
    bool died = false;
    for (int i = 0; i < 40 && !died; ++i) {
        died = game.tick().died;
    }
    check(died, "змейка умирает о стену");
    check(!game.alive(), "после смерти игра неактивна");
    check(game.snake().front().x == game.width() - 1, "голова осталась на последней клетке");
}

void testSelfCollision() {
    snake::Game game(30, 16, 13);
    feed(game, 3);  // длина 6
    game.setDirection(snake::Direction::Up);
    game.tick();
    game.setDirection(snake::Direction::Left);
    game.tick();
    game.setDirection(snake::Direction::Down);
    const auto result = game.tick();
    check(result.died, "змейка умирает о собственное тело");
}

void testTailChaseAllowed() {
    snake::Game game(30, 16, 13);
    feed(game, 1);  // длина 4
    game.setDirection(snake::Direction::Up);
    game.tick();
    game.setDirection(snake::Direction::Left);
    game.tick();
    game.setDirection(snake::Direction::Down);
    const auto result = game.tick();
    check(!result.died && game.alive(), "в уходящий хвост врезаться можно");
}

void testBonus() {
    snake::Game game(30, 16, 17);
    feed(game, 3);
    check(!game.bonus().active(), "до четвёртого яблока бонуса нет");
    feed(game, 1);
    check(game.bonus().active(), "после четвёртого яблока появляется бонус");

    const auto before = game.score();
    game.setBonus(ahead(game), 10);
    const auto result = game.tick();
    check(result.ateBonus, "золотое яблоко съедено");
    check(game.score() == before + snake::Game::kBonusScore, "бонус даёт дополнительные очки");
    check(!game.bonus().active(), "после поедания бонус исчезает");
}

void testBonusExpires() {
    snake::Game game(30, 16, 19);
    game.setBonus({0, 0}, 2);
    game.tick();
    check(game.bonus().active() && game.bonus().ticksLeft == 1, "бонус тикает");
    game.tick();
    check(!game.bonus().active(), "бонус исчезает по таймеру");
}

void testReset() {
    snake::Game game(24, 16, 23);
    feed(game, 2);
    game.setDirection(snake::Direction::Up);
    game.tick();
    game.reset();
    check(game.snake().size() == 3 && game.score() == 0 && game.level() == 1,
          "reset возвращает начальное состояние");
    check(game.direction() == snake::Direction::Right, "reset сбрасывает направление");
    check(!game.bonus().active(), "reset убирает бонус");
}

void testDeterminism() {
    snake::Game a(24, 16, 99);
    snake::Game b(24, 16, 99);
    check(a.food() == b.food(), "одинаковое зерно — одинаковое первое яблоко");
    a.tick();
    b.tick();
    check(a.food() == b.food(), "и дальше яблоки совпадают");
}

}  // namespace

int main() {
    testInitialState();
    testMovement();
    testNoReverse();
    testQueuedTurns();
    testFoodAndLevels();
    testWallCollision();
    testSelfCollision();
    testTailChaseAllowed();
    testBonus();
    testBonusExpires();
    testReset();
    testDeterminism();

    if (failures == 0) {
        std::printf("\nВсе тесты прошли.\n");
        return 0;
    }
    std::printf("\nПровалено тестов: %d\n", failures);
    return 1;
}
