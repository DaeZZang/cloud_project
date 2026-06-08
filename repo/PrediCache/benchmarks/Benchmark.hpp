#ifndef PREDICACHE_BENCHMARK_HPP
#define PREDICACHE_BENCHMARK_HPP

#ifdef PERF_EVENT
#include "../third_party/PerfEvent.hpp"
#endif

#include <iostream>
#include <thread>
#include <vector>
#include "../btree.hpp"
#include "../cbp.hpp"
#include "shared/Types.hpp"

extern const char* systemName;

const uint64_t statDiff = 1e8;

template<class Fn>
void parallel_for(uint64_t begin, uint64_t end, uint64_t nthreads, Fn fn) {
   std::vector<std::thread> threads;
   uint64_t n = end-begin;
   if (n<nthreads)
      nthreads = n;
   uint64_t perThread = n/nthreads;
   for (unsigned i=0; i<nthreads; i++) {
      threads.emplace_back([&,i]() {
          uint64_t b = (perThread*i) + begin;
          uint64_t e = (i==(nthreads-1)) ? end : (b+perThread);
          fn(i, b, e);
      });
   }
   for (auto& t : threads)
      t.join();
}

class Benchmark {
public:
    Benchmark() = delete;
    Benchmark(uint64_t nThreads, uint64_t datasize, uint64_t runForSec): nThreads(nThreads), datasize(datasize), runForSec(runForSec) {};
    virtual std::string getName() = 0;
    void run() {
       runInner();
#ifdef PERF_EVENT
       e.stopCounters();
       e.printReport(std::cout, totalTx); // use n as scale factor
       std::cout << std::endl;
#endif
    }
    virtual ~Benchmark() = default;
    std::atomic<uint64_t> txProgress{0};
    uint64_t nThreads;
    std::atomic<uint64_t> datasize;
    uint64_t runForSec;
    std::atomic<bool> keepRunning{true};
    std::atomic<u64> totalTx{0};
protected:
   virtual void runInner() = 0; // This has to set the worker thread id!
   void startStatThread() {
     std::cerr << "space: " << (bm.allocCount.load()*pageSize)/(float)bm.gb << " GB " << endl;

     auto statFn = [&]() {
       auto benchmarkName = getName();
       std::cout << "ts,tx,rmb,wmb,system,threads,datasize,workload,batch,writeCount,readCount"
#ifdef COPY_STATS
        << ",copyCount,opsCount"
#endif
        << std::endl;
       u64 cnt = 0;
       for (uint64_t i=0; i<runForSec; i++) {
         sleep(1);
         u64 readCount = bm.readCount.exchange(0);
         u64 writeCount = bm.writeCount.exchange(0);
         float rmb = (readCount*pageSize)/(1024.0*1024);
         float wmb = (writeCount*pageSize)/(1024.0*1024);
         u64 prog = txProgress.exchange(0);
         totalTx += prog;
         cbp::tpbOnWindow(prog);
         cout << cnt++ << "," << prog << "," << rmb << "," << wmb << "," << systemName << "," << nThreads << ","
            << datasize << "," << benchmarkName << "," << bm.batch
            << "," << writeCount << "," << readCount
#ifdef COPY_STATS
          << "," << bm.copyCount.exchange(0) << "," << bm.opsCount.exchange(0)
#endif
          << std::endl;
       }
       keepRunning = false;
     };

     std::thread statThread(statFn);
     statThread.detach();
#ifdef PERF_EVENT
      e.startCounters();
#endif
   }

#ifdef PERF_EVENT
   PerfEvent e;
#endif
};

#endif //PREDICACHE_BENCHMARK_HPP
