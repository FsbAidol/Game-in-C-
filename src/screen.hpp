// Экранный буфер: сетка ячеек с цветом и глифом.
// Умеет отдавать ANSI-кадр для терминала или простой текст для отладки.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ui {

// 256-цветная палитра, общая для всех экранов.
namespace pal {
constexpr int bgDeep = 233;
constexpr int bgBoard = 234;
constexpr int bgPanel = 235;
constexpr int bgBox = 236;
constexpr int border = 22;
constexpr int borderDim = 238;
constexpr int text = 252;
constexpr int textBright = 255;
constexpr int dim = 244;
constexpr int accent = 46;
constexpr int accentSoft = 82;
constexpr int snakeBody = 28;
constexpr int snakeBodyAlt = 22;
constexpr int snakeHead = 46;
constexpr int apple = 203;
constexpr int gold = 226;
constexpr int danger = 196;
constexpr int warn = 220;
}  // namespace pal

std::size_t utf8Len(const std::string& text);
std::vector<std::string> utf8Glyphs(const std::string& text);

struct Cell {
    std::string glyph = " ";
    int fg = -1;
    int bg = -1;
};

class Screen {
public:
    Screen(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    void clear(int fg = -1, int bg = -1);
    void put(int row, int col, const std::string& glyph, int fg = -1, int bg = -1);
    void text(int row, int col, const std::string& text, int fg = -1, int bg = -1);
    void textCentered(int row, int left, int right, const std::string& text, int fg = -1,
                      int bg = -1);
    void fill(int row, int col, int height, int width, int fg = -1, int bg = -1,
              const std::string& glyph = " ");
    void frame(int row, int col, int height, int width, int fg, int bg);

    std::string ansi() const;
    std::string plain() const;

private:
    Cell* at(int row, int col);
    const Cell* at(int row, int col) const;

    int width_;
    int height_;
    std::vector<Cell> cells_;
};

struct Layout {
    int termW = 0;
    int termH = 0;

    int fieldX = 1;
    int fieldY = 4;
    int fieldW = 60;
    int fieldH = 30;

    bool hasPanel = false;
    int panelX = 0;
    int panelY = 4;
    int panelW = 0;
    int panelH = 30;

    int innerX() const { return fieldX + 1; }
    int innerY() const { return fieldY + 1; }
    int innerW() const { return fieldW - 2; }
    int innerH() const { return fieldH - 2; }
};

// wantW/wantH — желаемый размер игрового поля без рамки (0 — подобрать самому).
Layout computeLayout(int termW, int termH, int wantW = 0, int wantH = 0,
                     bool allowPanel = true);

}  // namespace ui
