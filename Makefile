
BIN_TARGET = vclpp
SRC_FILE   = vclpp_main.cpp
CXXFLAGS   = -std=c++14 -O2 -Wall -Wextra -pedantic

all:
	$(CXX) $(CXXFLAGS) $(SRC_FILE) -o $(BIN_TARGET)

clean:
	rm -f *.o
	rm -f $(BIN_TARGET)

