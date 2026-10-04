
# 'make' builds vclpp; 'make test' builds it and runs the tests in tests/.
# CXX and BIN_TARGET can be overridden on the command line, as the quake2-ps2
# build does to put the binary in its own build directory. All of the sources
# compile in one command, so no object files are left in the source tree.

BIN_TARGET  = vclpp
PARSE_UTILS = external/parse-utils
SRC_FILES   = $(wildcard src/*.cpp)
CXXSTD      = -std=c++17
CXXFLAGS    = -O2 -Wall -Wextra -pedantic

all: $(PARSE_UTILS)/lexer.hpp
	$(CXX) $(CXXSTD) $(CXXFLAGS) -I$(PARSE_UTILS) $(SRC_FILES) -o $(BIN_TARGET)

# parse-utils is a git submodule.
$(PARSE_UTILS)/lexer.hpp:
	@echo "$(PARSE_UTILS) is empty - run 'git submodule update --init'"
	@exit 1

test: all
	@sh tests/run_tests.sh $(abspath $(BIN_TARGET))

clean:
	rm -f $(BIN_TARGET)

.PHONY: all test clean
