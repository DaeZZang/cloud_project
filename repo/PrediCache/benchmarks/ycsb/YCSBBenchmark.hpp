

#ifndef PREDICACHE_YCSBBENCHMARK_HPP
#define PREDICACHE_YCSBBENCHMARK_HPP

#include "../Benchmark.hpp"
#include "../shared/Schema.hpp"
#include "../PrediCacheAdapter.hpp"

using YCSBKey = u64;
using YCSBPayload = BytesPayload<120>;
using KVTable = Relation<YCSBKey, YCSBPayload>;

class YCSBBenchmark : public Benchmark {
public:
    using Benchmark::Benchmark;
    std::string getName() override { return "ycsb"; }
    void runInner() override;
};

#endif //PREDICACHE_YCSBBENCHMARK_HPP
