#ifndef PREDICACHE_SCRAMBLEDZIPFGENERATOR_HPP
#define PREDICACHE_SCRAMBLEDZIPFGENERATOR_HPP

#include "../shared/Types.hpp"
#include "ZipfGenerator.hpp"

class ScrambledZipfGenerator
{
public:
    u64 min, max, n;
    double theta;
    ZipfGenerator zipf_generator;
    // 10000000000ul
    // [min, max)
    ScrambledZipfGenerator(u64 min, u64 max, double theta) : min(min), max(max), n(max - min), zipf_generator((max - min) * 2, theta) {}
    u64 rand();
};

#endif //PREDICACHE_SCRAMBLEDZIPFGENERATOR_HPP
