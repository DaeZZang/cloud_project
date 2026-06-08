//
//

#include "RndReadBenchmark.hpp"
#include "../../utils/RandomGenerator.hpp"

void RndReadBenchmark::runInner() {
  BTree bt;
  bt.splitOrdered = true;

  {
    // insert
    parallel_for(0, datasize, max(nThreads, static_cast<uint64_t>(std::thread::hardware_concurrency() / 2)), [&](uint64_t worker, uint64_t begin, uint64_t end) {
      workerThreadId = worker;
      array<u8, 120> payload;
      for (u64 i=begin; i<end; i++) {
        union { u64 v1; u8 k1[sizeof(u64)]; };
        v1 = __builtin_bswap64(i);
        memcpy(payload.data(), k1, sizeof(u64));
        bt.insert({k1, sizeof(KeyType)}, payload);
      }
    });
  }

  bm.resetStats();

  startStatThread();

  parallel_for(0, nThreads, nThreads, [&](uint64_t worker, uint64_t begin, uint64_t end) {
    workerThreadId = worker;
    u64 cnt = 0;
    u64 start = rdtsc();
    while (keepRunning.load()) {
      union { u64 v1; u8 k1[sizeof(u64)]; };
      v1 = __builtin_bswap64(RandomGenerator::getRand<u64>(0, datasize));

      array<u8, 120> payload;
      bool succ = bt.lookup({k1, sizeof(u64)}, [&](span<u8> p) {
          memcpy(payload.data(), p.data(), p.size());
      });
      assert(succ);
      assert(memcmp(k1, payload.data(), sizeof(u64))==0);

      cnt++;
      u64 stop = rdtsc();
      if ((stop-start) > statDiff) {
         txProgress += cnt;
         start = stop;
         cnt = 0;
      }
    }
    txProgress += cnt;
    });
}


