#ifndef PREDICACHE_ZIPFGENERATOR_HPP
#define PREDICACHE_ZIPFGENERATOR_HPP

#include "../shared/Types.hpp"

class ZipfGenerator
{
    // -------------------------------------------------------------------------------------
private:
    u64 n;
    double theta;
    // -------------------------------------------------------------------------------------
    double alpha, zetan, eta;
    // -------------------------------------------------------------------------------------
    double zeta(u64 n, double theta);

public:
    // [0, n)
    ZipfGenerator(uint64_t ex_n, double theta);
    // uint64_t rand(u64 new_n);
    uint64_t rand();
};

#endif //PREDICACHE_ZIPFGENERATOR_HPP
