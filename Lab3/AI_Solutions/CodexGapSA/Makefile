CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic
dp: src/db.cpp src/placer.cpp src/db.hpp
	$(CXX) $(CXXFLAGS) src/db.cpp src/placer.cpp -o dp
.PHONY: clean
clean:
	rm -f dp
