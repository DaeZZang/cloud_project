#include <algorithm>
#include <atomic>
#include <cassert>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <span>
#include <thread>
#include <vector>

#include <shared_mutex>
#include <sys/mman.h>
#include <unistd.h>

__thread uint16_t workerThreadId = 0;

#include "buffer_manager.hpp"
#include "benchmarks/Benchmark.hpp"
#include "benchmarks/tpcc/TPCCBenchmark.hpp"
#include "benchmarks/rndread/RndReadBenchmark.hpp"
#include "benchmarks/ycsb/YCSBBenchmark.hpp"

const char* systemName;

BufferManager bm;

using namespace std;

int main(int argc, char** argv) {
   unsigned nthreads = envOr("THREADS", 1);
   u64 n = envOr("DATASIZE", 10);
   u64 runForSec = envOr("RUNFOR", 30);
   bool isRndread = envOr("RNDREAD", 0);
   std::string defaultWorkload = isRndread ? "rndread" : "tpcc";
   std::string benchmarkName{getenv("WORKLOAD")? getenv("WORKLOAD") : defaultWorkload};

   std::transform(benchmarkName.begin(), benchmarkName.end(), benchmarkName.begin(),
                  [](unsigned char c){ return std::tolower(c); });

   std::unique_ptr<Benchmark> benchmark;

   if (benchmarkName == "tpc-c" || benchmarkName == "tpcc")
      benchmark = std::make_unique<TPCCBenchmark>(nthreads, n, runForSec);
   else if (benchmarkName == "rnd-read" || benchmarkName == "rndread")
      benchmark = std::make_unique<RndReadBenchmark>(nthreads, n, runForSec);
   else if (benchmarkName == "ycsb")
      benchmark = std::make_unique<YCSBBenchmark>(nthreads, n, runForSec);
   else
      die("unknown workload");

   systemName = "PrediCache";

   bm.readCount = 0;
   bm.writeCount = 0;

   benchmark->run();

   cerr << "space: " << (bm.allocCount.load()*pageSize)/(float)bm.gb << " GB " << endl;

   if (cbp::enabled) {
      const char* path = getenv("CBP_DUMP");
      FILE* f = path ? fopen(path, "w") : stderr;
      if (f) {
         cbp::dumpFinal(f);
         if (path) fclose(f);
      }
   }

   return 0;
}
