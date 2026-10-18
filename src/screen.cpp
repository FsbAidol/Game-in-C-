#include "screen.hpp"

#include <algorithm>
#include <string>

namespace ui {
namespace {

bool isContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }

std::string sgr(int fg, int bg) {
    std::string out;
    if (fg >= 0) {
        out += "\x1b[38;5;" + std::to_string(fg) + "m";
    } else {
        out += "\x1b[39m";
    }
    if (bg >= 0) {
        out += "\x1b[48;5;" + std::to_string(bg) + "m";
    } else {
        out += "\x1b[49m";
    }
    return out;
}

}  // namespace

std::vector<std::string> utf8Glyphs(const std::string& text) {
    std::vector<std::string> glyphs;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (i + len > text.size()) len = 1;
        for (std::size_t k = 1; k < len; ++k) {
            if (!isContinuation(static_cast<unsigned char>(text[i + k]))) {
                len = 1;
                break;
            }
        }
        glyphs.push_back(text.substr(i, len));
        i += len;
    }
    return glyphs;
}

std::size_t utf8Len(const std::string& text) { return utf8Glyphs(text).size(); }

Screen::Screen(int width, int height)
    : width_(std::max(1, width)), height_(std::max(1, height)) {
    cells_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
}

void Screen::clear(int fg, int bg) {
    for (auto& cell : cells_) {
        cell.glyph = " ";
        cell.fg = fg;
        cell.bg = bg;
    }
}

Cell* Screen::at(int row, int col) {
    if (row < 1 || row > height_ || col < 1 || col > width_) return nullptr;
    return &cells_[static_cast<std::size_t>(row - 1) * static_cast<std::size_t>(width_) +
                   static_cast<std::size_t>(col - 1)];
}

const Cell* Screen::at(int row, int col) const {
    if (row < 1 || row > height_ || col < 1 || col > width_) return nullptr;
    return &cells_[static_cast<std::size_t>(row - 1) * static_cast<std::size_t>(width_) +
                   static_cast<std::size_t>(col - 1)];
}

void Screen::put(int row, int col, const std::string& glyph, int fg, int bg) {
    if (Cell* cell = at(row, col)) {
        cell->glyph = glyph.empty() ? " " : glyph;
        cell->fg = fg;
        cell->bg = bg;
    }
}

void Screen::text(int row, int col, const std::string& value, int fg, int bg) {
    int x = col;
    for (const auto& glyph : utf8Glyphs(value)) {
        put(row, x, glyph, fg, bg);
        ++x;
    }
}

void Screen::textCentered(int row, int left, int right, const std::string& value, int fg, int bg) {
    const int width = right - left + 1;
    if (width <= 0) return;
    const int len = static_cast<int>(utf8Len(value));
    int x = left + std::max(0, (width - len) / 2);
    text(row, x, value, fg, bg);
}

void Screen::fill(int row, int col, int height, int width, int fg, int bg,
                  const std::string& glyph) {
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            put(row + r, col + c, glyph, fg, bg);
        }
    }
}

void Screen::frame(int row, int col, int height, int width, int fg, int bg) {
    if (height < 2 || width < 2) return;
    for (int c = 1; c < width - 1; ++c) {
        put(row, col + c, "─", fg, bg);
        put(row + height - 1, col + c, "─", fg, bg);
    }
    for (int r = 1; r < height - 1; ++r) {
        put(row + r, col, "│", fg, bg);
        put(row + r, col + width - 1, "│", fg, bg);
    }
    put(row, col, "╭", fg, bg);
    put(row, col + width - 1, "╮", fg, bg);
    put(row + height - 1, col, "╰", fg, bg);
    put(row + height - 1, col + width - 1, "╯", fg, bg);
}

std::string Screen::ansi() const {
    std::string out;
    out.reserve(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 8);
    out += "\x1b[H";
    int lastFg = -2;
    int lastBg = -2;
    for (int row = 1; row <= height_; ++row) {
        out += "\x1b[" + std::to_string(row) + ";1H";
        lastFg = -2;
        lastBg = -2;
        for (int col = 1; col <= width_; ++col) {
            const Cell* cell = at(row, col);
            if (cell == nullptr) continue;
            if (cell->fg != lastFg || cell->bg != lastBg) {
                out += sgr(cell->fg, cell->bg);
                lastFg = cell->fg;
                lastBg = cell->bg;
            }
            out += cell->glyph;
        }
    }
    out += "\x1b[0m";
    return out;
}

std::string Screen::plain() const {
    std::string out;
    for (int row = 1; row <= height_; ++row) {
        std::string line;
        for (int col = 1; col <= width_; ++col) {
            const Cell* cell = at(row, col);
            line += (cell != nullptr) ? cell->glyph : " ";
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += line;
        if (row != height_) out += '\n';
    }
    return out;
}

Layout computeLayout(int termW, int termH, int wantW, int wantH, bool allowPanel) {
    Layout layout;
    layout.termW = std::max(termW, 40);
    layout.termH = std::max(termH, 14);

    const int top = 4;
    const int availableH = std::max(12, layout.termH - top - 1);

    int maxFieldW = layout.termW - 4;
    if (wantW > 0) {
        maxFieldW = std::min(maxFieldW, wantW + 2);
    } else {
        maxFieldW = std::min(maxFieldW, 66);
    }
    int fieldW = std::max(28, maxFieldW);
    int fieldH = wantH > 0 ? wantH + 2 : 34;
    fieldH = std::min(fieldH, availableH);
    fieldH = std::max(fieldH, 12);

    const int panelW = 30;
    const bool panelFits = allowPanel && layout.termW >= 84 && fieldH >= 18 &&
                           layout.termW - fieldW - 4 >= panelW;
    int groupW = fieldW + (panelFits ? panelW + 2 : 0);
    if (groupW > layout.termW - 2) {
        fieldW = std::max(24, layout.termW - 2 - (panelFits ? panelW + 2 : 0));
        groupW = fieldW + (panelFits ? panelW + 2 : 0);
    }

    layout.fieldW = fieldW;
    layout.fieldH = fieldH;
    layout.fieldX = std::max(1, (layout.termW - groupW) / 2 + 1);
    layout.fieldY = top + std::max(0, (availableH - fieldH) / 2);

    layout.hasPanel = panelFits;
    if (panelFits) {
        layout.panelW = panelW;
        layout.panelH = fieldH;
        layout.panelX = layout.fieldX + fieldW + 2;
        layout.panelY = layout.fieldY;
    }
    return layout;
}

}  // namespace ui
