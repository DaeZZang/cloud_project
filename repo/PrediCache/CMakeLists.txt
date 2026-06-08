cmake_minimum_required(VERSION 3.10)
project(predicache)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_COMPILER g++-14)
set(CMAKE_CC_COMPILER gcc-14)

# Define your source files
set(SOURCE_FILES
        predicache.cpp
        benchmarks/Benchmark.hpp
        benchmarks/PrediCacheAdapter.hpp
        benchmarks/rndread/RndReadBenchmark.cpp
        benchmarks/rndread/RndReadBenchmark.hpp
        benchmarks/tpcc/TPCCBenchmark.cpp
        benchmarks/tpcc/TPCCBenchmark.hpp
        benchmarks/tpcc/TPCCWorkload.hpp
        benchmarks/ycsb/YCSBBenchmark.cpp
        benchmarks/ycsb/YCSBBenchmark.hpp
        utils/RandomGenerator.hpp
        benchmarks/utils/ScrambledZipfGenerator.cpp
        benchmarks/utils/ScrambledZipfGenerator.hpp
        benchmarks/utils/ZipfGenerator.cpp
        benchmarks/utils/ZipfGenerator.hpp
        buffer_manager.hpp
        uring_interface.hpp
        libaio_interface.hpp
        page.hpp
        ht.hpp
        buffer_frame.hpp
        btree.cpp
        btree.hpp
)


add_compile_options(-fnon-call-exceptions -fasynchronous-unwind-tables)


add_executable(predicache ${SOURCE_FILES})


# Link the required libraries, in this case, libaio
target_link_libraries(predicache aio stdc++exp uring)
