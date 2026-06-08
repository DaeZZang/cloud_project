#ifndef PREDICACHE_RNDREADBENCHMARK_HPP
#define PREDICACHE_RNDREADBENCHMARK_HPP

#include "../Benchmark.hpp"

class RndReadBenchmark : public Benchmark {
public:
    using Benchmark::Benchmark;
    std::string getName() override { return "rndread"; }
    void runInner() override;
};

#endif //PREDICACHE_RNDREADBENCHMARK_HPP
