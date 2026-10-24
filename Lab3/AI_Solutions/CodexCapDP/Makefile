# Plain make alternative to CMake: produces ./dp
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

dp: src/*.cpp src/*.hpp
	$(CXX) $(CXXFLAGS) -pthread -o $@ src/*.cpp

clean:
	rm -f dp

test:
	python3 tests/run_tests.py

.PHONY: clean test
