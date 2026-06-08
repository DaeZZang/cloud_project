

// -------------------------------------------------------------------------------------
#include "YCSBBenchmark.hpp"
#include <iostream>
#include "../../utils/RandomGenerator.hpp"
#include "../utils/ScrambledZipfGenerator.hpp"
#include "../shared/Types.hpp"

void YCSBBenchmark::runInner() {
  double zipfFactor = doubleEnvOr("ZIPF_FACTOR", 0.9);
  u64 opsPerTx = envOr("OPS_PER_TX", 1);
  u64 readRatio = envOr("READ_RATIO", 100);

  predicacheAdapter<KVTable> table;

  // Insert values
  const u64 n = datasize;
  // -------------------------------------------------------------------------------------
  parallel_for(0, n, max(nThreads, static_cast<uint64_t>(std::thread::hardware_concurrency()) / 2), [&](u64 worker, u64 begin, u64 end) {
    workerThreadId = worker;
    for (u64 i = begin; i < end; i++) {
      YCSBPayload payload;
      RandomGenerator::getRandString(reinterpret_cast<u8 *>(&payload), sizeof(YCSBPayload));
      YCSBKey key = i;
      table.insert({key}, {payload});
    }
  });

  auto zipfRandom = ScrambledZipfGenerator(0, datasize, zipfFactor);

  bm.resetStats();

  startStatThread();

   parallel_for(0, nThreads, nThreads, [&](u64 worker, u64 begin, u64 end) {
      workerThreadId = worker;
      u64 cnt = 0;
      u64 start = rdtsc();
      while (keepRunning) {
         YCSBKey key;
         if (zipfFactor == 0) {
            key = RandomGenerator::getRandU64(0, datasize);
         } else {
            key = zipfRandom.rand();
         }
         assert(key < datasize);
         YCSBPayload result;
         for (u64 op_i = 0; op_i < opsPerTx; op_i++) {
            if (readRatio == 100 || RandomGenerator::getRandU64(0, 100) < readRatio) {
               table.lookup1({key}, [&](const KVTable &) {});  // result = record.my_payload;
            } else {
               table.update1({key}, [&](KVTable &rec) {
                   rec.my_payload = result;
               });
            }
         }
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