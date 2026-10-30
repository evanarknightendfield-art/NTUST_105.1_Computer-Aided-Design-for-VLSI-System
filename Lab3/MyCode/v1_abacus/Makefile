# Plain make alternative to CMake: produces ./dp
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

dp: src/*.cpp src/*.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/*.cpp

clean:
	rm -f dp

.PHONY: clean
