SOURCES := $(shell find $(SOURCEDIR) -name '*pp')

predicache: $(SOURCES) Makefile
	g++ -DNDEBUG -std=c++23 -O3 -g -fasynchronous-unwind-tables benchmarks/**/*.cpp btree.cpp predicache.cpp -o predicache -laio

uring: $(SOURCES) Makefile
	g++ -DNDEBUG -DUSE_URING -std=c++23 -O3 -g -fasynchronous-unwind-tables benchmarks/**/*.cpp btree.cpp predicache.cpp -o predicache -luring

perfevent: $(SOURCES) Makefile
	g++ -DNDEBUG -DPERF_EVENT -std=c++23 -O3 -g -fasynchronous-unwind-tables benchmarks/**/*.cpp btree.cpp predicache.cpp -o predicache -laio

debug: $(SOURCES)  Makefile
	g++ -DDEBUG -std=c++23 -O0 -g -fasynchronous-unwind-tables benchmarks/**/*.cpp btree.cpp predicache.cpp -o predicache -laio

debuglocks: $(SOURCES) Makefile
	g++ -DDEBUG -DADD_LOCK_DEBUG_INFO -std=c++23 -O0 -g -fasynchronous-unwind-tables benchmarks/**/*.cpp btree.cpp predicache.cpp -o predicache -lstdc++_libbacktrace  -laio

rr: $(SOURCES) Makefile
	g++ -DDEBUG -DUSE_POSIX_IO -std=c++23 -O0 -g -fasynchronous-unwind-tables benchmarks/**/*.cpp btree.cpp predicache.cpp -o predicache -laio

clean:
	rm predicache

all: predicache
