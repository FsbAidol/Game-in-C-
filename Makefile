CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Isrc

BIN := build/snake-ui
SRC := src/app.cpp src/game.cpp src/main.cpp src/screen.cpp src/term.cpp
OBJ := $(SRC:src/%.cpp=build/obj/%.o)
HEADERS := $(wildcard src/*.hpp)
TEST_BIN := build/tests/game_tests

.PHONY: all run dump dump-game test serve check-ui clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@
	@echo "Готово: ./$(BIN)"

build/obj/%.o: src/%.cpp $(HEADERS) | build/obj
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

build/obj build/tests:
	mkdir -p $@

$(TEST_BIN): tests/test_game.cpp src/game.cpp src/game.hpp | build/tests
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tests/test_game.cpp src/game.cpp -o $@

run: $(BIN)
	./$(BIN)

dump: $(BIN)
	./$(BIN) --size 100x38 --seed 7 --dump title

dump-game: $(BIN)
	./$(BIN) --size 100x38 --seed 7 --dump game --dump-ticks 16

test: $(TEST_BIN)
	./$(TEST_BIN)

check-ui: $(BIN)
	@./$(BIN) --size 100x38 --seed 7 --dump title | grep -q "S N A K E" \
		&& echo "ok   титульный экран рисуется"
	@./$(BIN) --size 100x38 --seed 7 --dump game | grep -q "ОЧКИ" \
		&& echo "ok   игровой экран рисуется"

serve: $(BIN)
	node web/server.js

clean:
	rm -rf build
