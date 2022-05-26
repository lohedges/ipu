.DEFAULT_GOAL := all

CXX := clang++
CXXFLAGS := -O3 -Isrc

ABIFLAG := 0
POPLARFLAGS :=-std=c++17 -L/opt/poplar/lib -lpoplar -lpoputil

all:
	$(CXX) $(CXXFLAGS) -D_GLIBCXX_USE_CXX11_ABI=$(ABIFLAG) $(POPLARFLAGS) $(OPTFLAGS) src/main.cpp -o ipu_example

clean:
	rm -f ipu_example
