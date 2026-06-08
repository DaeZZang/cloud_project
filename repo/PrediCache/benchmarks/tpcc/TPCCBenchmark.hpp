

#ifndef PREDICACHE_TPCCBENCHMARK_HPP
#define PREDICACHE_TPCCBENCHMARK_HPP

#include "../Benchmark.hpp"
#include "../PrediCacheAdapter.hpp"

class TPCCBenchmark : public Benchmark {
public:
    using Benchmark::Benchmark;

    void runInner() override;
    std::string getName() override { return "tpcc"; }
};

#endif //PREDICACHE_TPCCBENCHMARK_HPP
