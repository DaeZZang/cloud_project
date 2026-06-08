#ifndef PREDICACHE_BUFFER_MANAGER_HPP
#define PREDICACHE_BUFFER_MANAGER_HPP

#ifndef USE_URING
#define USE_LIBAIO
#endif

#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include "ht.hpp"
#include "page.hpp"
#include "third_party/concurrentqueue.hpp"
#include "third_party/libdivide.h"
#ifdef USE_LIBAIO
#include "libaio_interface.hpp"
#else
#include "uring_interface.hpp"
#endif
#include "utils/RandomGenerator.hpp"
#include "cbp.hpp"

//#define USE_PROCESS_MADV

extern __thread uint16_t workerThreadId;

constexpr u64 nextPowerOf2(u64 n) {
   if (n == 0) return 1;

   n--;

   n |= n >> 1;
   n |= n >> 2;
   n |= n >> 4;
   n |= n >> 8;
   n |= n >> 16;
   n |= n >> 32;

   return n + 1;
}

static u64 promoteOptimisticChance = 32;
static u64 promoteOptimisticMask = nextPowerOf2(promoteOptimisticChance) - 1;

static u64 promoteSharedChance = 32;
static u64 promoteSharedMask = nextPowerOf2(promoteSharedChance) - 1;

static u64 promoteExclusiveChance = 32;
static u64 promoteExclusiveMask = nextPowerOf2(promoteExclusiveChance) - 1;

static u64 promoteDisplaceChance = 16;
static u64 promoteDisplaceMask = nextPowerOf2(promoteDisplaceChance) - 1;

// When DISABLE_PRED=1, predictive-translation fast path is disabled (Traditional-baseline mode).
static bool disablePredict = false;

static inline u64 envOrU(const char* env, u64 def) {
   const char* v = getenv(env);
   if (!v) return def;
   char* end = nullptr;
   u64 x = strtoull(v, &end, 10);
   return (end && end != v) ? x : def;
}

static void initPromoteChances() {
   promoteOptimisticChance = envOrU("PROMO_OPT", promoteOptimisticChance);
   promoteOptimisticMask   = nextPowerOf2(promoteOptimisticChance) - 1;
   promoteSharedChance     = envOrU("PROMO_SHARED", promoteSharedChance);
   promoteSharedMask       = nextPowerOf2(promoteSharedChance) - 1;
   promoteExclusiveChance  = envOrU("PROMO_EXCL", promoteExclusiveChance);
   promoteExclusiveMask    = nextPowerOf2(promoteExclusiveChance) - 1;
   promoteDisplaceChance   = envOrU("PROMO_DISPLACE", promoteDisplaceChance);
   promoteDisplaceMask     = nextPowerOf2(promoteDisplaceChance) - 1;
   const char* dp = getenv("DISABLE_PRED");
   disablePredict = dp && (dp[0] == '1' || dp[0] == 'y' || dp[0] == 'Y');
   cbp::init();
}

inline u64 hashPID(PID pid, u64 size) {
   u64 k = pid;
   const u64 m = 0xc6a4a7935bd1e995;
   const int r = 47;
   u64 h = 0x8445d61a4e774912 ^ (8 * m);
   k *= m;
   k ^= k >> r;
   k *= m;
   h ^= k;
   h *= m;
   h ^= h >> r;
   h *= m;
   h ^= h >> r;
   u64 result = h;
   return result;
   // These two lines will make the hash function return pid for pid < size which optimizes pure in-mem cases
   //u64 mask = -static_cast<u64>(pid >= size);
   //return (mask & result) | (~mask & pid);
}

struct BufferManager {
    static const u64 mb = 1024ull * 1024;
    static const u64 gb = 1024ull * 1024 * 1024;
    u64 poolSize;
    u64 frameCount;
#ifdef USE_LIBAIO
    vector<LibaioInterface> ioInterface;
#else
   vector<UringInterface> ioInterface;
#endif

    vector<int> blockfds;

    alignas(64) ResidentPageSet residentSet;
    alignas(64) atomic<u64> allocCount;

   atomic<u64> readCount;
   atomic<u64> writeCount;
#ifdef COPY_STATS
   atomic<u64> copyCount;
   atomic<u64> opsCount;
#endif

    alignas(64) Page* virtMem;
    std::atomic<PID>* virtMemPids;
    u64 batch;

    u64 usablePhysCount;
    u64 minPages;

    alignas(64) HTBufferManager ht;

    alignas(64) libdivide::divider<u64> frameCountDivider;

    alignas(64) std::atomic<u64> evictsHTCache;

#ifdef USE_PROCESS_MADV
   alignas(64) int pidfd;
#endif

    BufferManager();

    ~BufferManager() {
       munmap(virtMem, poolSize);
       munmap(virtMemPids, frameCount * sizeof(PID));
       for (int fd : blockfds)
          close(fd);
    }

    BufferFrame* fixX(PID pid);

    void unfixX(BufferFrame& bf);

    BufferFrame* fixS(PID pid);

    void unfixS(BufferFrame& bf);

    void tryPromote(PID pid);

    void promoteLocked(BufferFrame& bf);

    bool isValidPtr(void* page) {
       return (page >= virtMem) && (page < (virtMem + frameCount + 16));
    }

    Page* intendedFrame(u64 hash) const { return virtMem + (hash - hash / frameCountDivider * frameCount); }

    void ensureFreePages();

    BufferFrame* allocPage();

    BufferFrame* handleFault(PID pid);

    void readPageToHTCache(PID pid, Page* bufferFrame);

    void evict();

    Page* getFreePageHTCache(PID pid);

    /// flush all dirty pages to disk (not thread-safe)
    void flush();

    /// clear all pages (not thread-safe)
    void clear();

    bool decidePromotionOptimistic();

    bool decidePromotionShared();

    bool decidePromotionExclusive();

   bool trySetPresent(Page* page, PID pid) {
      size_t slot = page - virtMem;
      PID expected = std::numeric_limits<PID>::max();
      return virtMemPids[slot].compare_exchange_strong(expected, pid, std::memory_order::acq_rel);
   }

   void
   resetStats() {
      readCount = 0;
      writeCount = 0;
#ifdef COPY_STATS
      copyCount = 0;
      opsCount = 0;
#endif
   }
};


extern BufferManager bm;

inline u64 envOr(const char* env, u64 value) {
   if (getenv(env))
      return atof(getenv(env));
   return value;
}

inline double doubleEnvOr(const char* env, double value) {
   if (getenv(env))
      return atof(getenv(env));
   return value;
}

inline BufferManager::BufferManager() :
    poolSize(envOr("POOLGB", 1) * gb), frameCount(poolSize / pageSize),
    usablePhysCount(static_cast<u64>(static_cast<double>(frameCount) * 0.95)),
    batch(envOr("BATCH", 64)), residentSet(frameCount),
    ht(usablePhysCount * 2), frameCountDivider(frameCount) {

   initPromoteChances();
   poolSize = frameCount * pageSize;

   const char* path = getenv("BLOCK") ? getenv("BLOCK") : "/tmp/bm";
   // Split by comma
   char* token = strtok(const_cast<char*>(path), ",");
   while (token != nullptr) {
      blockfds.push_back(open(token, O_RDWR | O_DIRECT, S_IRWXU));
      if (blockfds.back() == -1) {
         cerr << "cannot open BLOCK device '" << token << "'" << endl;
         exit(EXIT_FAILURE);
      }
      token = strtok(nullptr, ",");
   }
   u64 virtAllocSize = poolSize + (1 << 16); // we allocate 64KB extra to prevent segfaults during optimistic reads

   virtMem = (Page*) mmap(NULL, virtAllocSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   madvise(virtMem, virtAllocSize, MADV_HUGEPAGE);
   madvise(virtMem, virtAllocSize, MADV_POPULATE_WRITE);

   virtMemPids = static_cast<std::atomic<PID>*>(
         mmap(NULL, frameCount * sizeof(PID), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

   memset(virtMemPids, 0xFF, frameCount * sizeof(PID));

   vector<Page*> pages;
   for (u64 i = 0; i < frameCount; i++) {
      pages.push_back(virtMem + i);
   }

   ht.freeList.enqueue_bulk(pages.data(), pages.size());

   if (virtMem == MAP_FAILED)
      die("mmap failed");

   ioInterface.reserve(maxWorkerThreads);
   for (unsigned i = 0; i < maxWorkerThreads; i++)
      ioInterface.emplace_back(blockfds);

   allocCount = 0;
   // pid 0 reserved for meta data
   BufferFrame* bf = allocPage();
   bf->state.unlockX();
   readCount = 0;
   writeCount = 0;

#ifdef USE_PROCESS_MADV
   pidfd = syscall(SYS_pidfd_open, getpid(), 0);
#endif

   cerr << "PrediCache "
        << "blk:" << path << " buffer-pool-size: " << poolSize / gb << "GB" << endl;
}

// allocated new page and fix it
[[gnu::noinline]] inline BufferFrame* BufferManager::allocPage() {
   u64 pid = allocCount++;
   u64 hash = hashPID(pid, frameCount);

   // This locks the page
   BufferFrame* bf = ht.insert(pid, hash);
   residentSet.insert(pid);

   bf->page = intendedFrame(hash);
   bf->intendedSlot = !disablePredict;

   if (!trySetPresent(bf->page, pid)) {
      bf->page = getFreePageHTCache(pid);
      bf->intendedSlot = false;
   }

   size_t slotI = bf->page - virtMem;
   virtMemPids[slotI] = pid;

   bf->page->setDirty(true);
   bf->page->pid = pid;

   return bf;
}

[[gnu::noinline]] inline BufferFrame* BufferManager::handleFault(PID pid) {
   // We read the page from disk and put it into htcache
   u64 hash = hashPID(pid, frameCount);
   BufferFrame* bf = ht.insert(pid, hash);
   if (bf != nullptr) {
      residentSet.insert(pid);
      assert(bf->state.getState() == PageState::Locked);
      assert(bf->pid == pid);
      Page* intendedPage = intendedFrame(hash);
      if (trySetPresent(intendedPage, pid)) {
         bf->page = intendedPage;
         bf->intendedSlot = !disablePredict;
      } else {
         Page* victim = getFreePageHTCache(pid);
         bf->page = victim;
         bf->intendedSlot = false;
      }
      readPageToHTCache(pid, bf->page);
      size_t slotI = bf->page - virtMem;
      virtMemPids[slotI] = pid;
      return bf;
   }
   return nullptr;
}

inline BufferFrame* BufferManager::fixX(PID pid) {
   u64 hash = hashPID(pid, frameCount);
   for (u64 repeatCounter = 0;; repeatCounter++) {
      auto accessResult = ht.access(pid, hash);
      BufferFrame*& bf = accessResult.first;
      u64& stateAndVersion = accessResult.second;
      if (bf == nullptr) [[unlikely]] {
         bf = handleFault(pid);
         if (bf == nullptr) {
            continue;
         }
         return bf;
      }
      PageState& ps = bf->state;
      if ((PageState::getState(stateAndVersion) == PageState::Unlocked || PageState::getState(stateAndVersion) == PageState::Marked) && ps.tryLockX(stateAndVersion)) {
         return bf;
      }
      yield(repeatCounter);
   }
}

inline BufferFrame* BufferManager::fixS(PID pid) {
   u64 hash = hashPID(pid, frameCount);
   for (u64 repeatCounter = 0;; repeatCounter++) {
      auto accessResult = ht.access(pid, hash);
      BufferFrame*& bf = accessResult.first;
      u64& stateAndVersion = accessResult.second;
      if (bf == nullptr) [[unlikely]] {
         bf = handleFault(pid);
         if (bf == nullptr) {
            continue;
         }
         bf->state.downgradeLock();
         return bf;
      }
      PageState& ps = bf->state;
      if (ps.tryLockS(stateAndVersion)) {
         return bf;
      }
      yield(repeatCounter);
   }
}

[[gnu::noinline]] inline void BufferManager::tryPromote(PID pid) {
   u64 hash = hashPID(pid, frameCount);
   for (u64 repeatCounter = 0; repeatCounter<100; repeatCounter++) {
      auto accessResult = ht.access(pid, hash);
      BufferFrame*& bf = accessResult.first;
      u64& stateAndVersion = accessResult.second;
      if (bf == nullptr)
         return;
      PageState &ps = bf->state;
      switch (PageState::getState(stateAndVersion)) {
         case PageState::Marked:
         case PageState::Unlocked: {
            if (intendedFrame(hash) == bf->page)
               return;
            Page* preferredFrame = intendedFrame(hash);
            PID replacingPID = virtMemPids[preferredFrame - virtMem];
            Page* replacingPIDIntendedFrame = intendedFrame(hashPID(replacingPID, frameCount));
            if (replacingPIDIntendedFrame == preferredFrame && (FastRandomGenerator::getRandU64() & promoteDisplaceMask) != 0) {
               return;
            }
            if (ps.tryLockX(stateAndVersion)) {
               if (virtMemPids[preferredFrame - virtMem] == pid) {
                  ps.unlockX();
                  return; // already promoted by someone else or cannot promote
               }
               promoteLocked(*bf);
               ps.unlockX();
               return;
            }
            break;
         }
      }
      yield(repeatCounter);
   }
}

[[gnu::noinline]] inline void BufferManager::promoteLocked(BufferFrame& bf) {
   u64 hash = hashPID(bf.pid, frameCount);
   Page* intendedSlotFrame = intendedFrame(hash);
   size_t intendedSlot = intendedSlotFrame - virtMem;
   if (!trySetPresent(intendedSlotFrame, bf.pid)) {
      // There's a page in the slot that we have to move away
      // we try to lock it
      PID pidMove = virtMemPids[intendedSlot].load();
      auto moveAccessResult = ht.access(pidMove, hashPID(bf.pid, frameCount));
      BufferFrame*& bfMove = moveAccessResult.first;
      u64& bfMoveV = moveAccessResult.second;
      if (bfMove == nullptr)
         return;
      if (PageState::getState(bfMoveV) != PageState::Unlocked || !bfMove->state.tryLockX(bfMoveV))
         return;

      Page* newMovePageSlot = intendedFrame(hashPID(pidMove, frameCount));
      if (trySetPresent(newMovePageSlot, pidMove)) {
         bfMove->intendedSlot = !disablePredict;
      } else {
         newMovePageSlot = getFreePageHTCache(pidMove);
         bfMove->intendedSlot = false;
      }

      memcpy(newMovePageSlot, intendedSlotFrame, pageSize);
#ifdef COPY_STATS
      ++copyCount;
#endif
      size_t newSlot = newMovePageSlot - virtMem;
      virtMemPids[newSlot] = pidMove;
      virtMemPids[intendedSlot] = bf.pid;
      bfMove->page = newMovePageSlot;
      bfMove->state.unlockX();
   }

   memcpy(intendedSlotFrame, bf.page, pageSize);
#ifdef COPY_STATS
   ++copyCount;
#endif
   size_t oldSlot = bf.page - virtMem;
   virtMemPids[oldSlot] = std::numeric_limits<PID>::max();
   ht.freeList.enqueue(bf.page);
   virtMemPids[intendedSlot] = bf.pid;
   bf.page = intendedSlotFrame;
   bf.intendedSlot = !disablePredict;
}


inline void BufferManager::unfixS(BufferFrame& bf) {
#ifdef COPY_STATS
   ++opsCount;
#endif
   PID pid = bf.pid;
   u64 hash = hashPID(pid, frameCount);
   bool inPreferred = (intendedFrame(hash) == bf.page);
   bf.state.unlockS();
   if (cbp::enabled) {
      cbp::noteAccess(cbp::classOf(hash), inPreferred);
      if (!inPreferred && ht.isInlined(&bf) && cbp::decideForClass(cbp::classOf(hash))) {
         cbp::noteAttempt(cbp::classOf(hash));
         tryPromote(pid);
      }
   } else {
      if (!inPreferred && ht.isInlined(&bf) && decidePromotionShared())
         tryPromote(pid);
   }
}

inline void BufferManager::unfixX(BufferFrame& bf) {
#ifdef COPY_STATS
   ++opsCount;
#endif
   PID pid = bf.pid;
   u64 hash = hashPID(pid, frameCount);
   bool inPreferred = (intendedFrame(hash) == bf.page);
   if (cbp::enabled) cbp::noteAccess(cbp::classOf(hash), inPreferred);
   bool decide = cbp::enabled ? cbp::decideForClass(cbp::classOf(hash))
                              : decidePromotionExclusive();
   if (!ht.isInlined(&bf) || inPreferred || !decide) {
      bf.state.unlockX();
   } else [[unlikely]] {
      Page* preferredFrame = intendedFrame(hash);
      PID replacingPID = virtMemPids[preferredFrame - virtMem];
      Page* replacingPIDIntendedFrame = intendedFrame(hashPID(replacingPID, frameCount));
      if (replacingPIDIntendedFrame != preferredFrame || (FastRandomGenerator::getRandU64() & promoteDisplaceMask) == 0) {
         if (cbp::enabled) cbp::noteAttempt(cbp::classOf(hash));
         promoteLocked(bf);
      }
      bf.state.unlockX();
   }
}

inline void BufferManager::readPageToHTCache(PID pid, Page* bufferFrame) {
   int ret = pread(blockfds[pid % blockfds.size()], bufferFrame, pageSize,  pageSize * (pid / blockfds.size()));
   assert(ret==pageSize);
   // int ret = ioInterface[workerThreadId].readPage(pid, bufferFrame);
   // assert(ret == 1);
   assert(!bufferFrame->dirty);
   readCount++;
}

[[gnu::noinline]] inline void BufferManager::evict() {

   u64 evicted = 0;   // Use eviction to evict a batch of 64 pages

   vector<PID> htEntriesToWrite;
   vector<BufferFrame*> htEntriesToWriteBfs;
   vector<Page*> htEntriesToWritePages;
   vector<PID> evictedHTCache;

   // 0. find candidates, lock dirty ones in shared mode
   u64 tries = 0;
   while (evicted < batch && tries < 10000) {
      tries += 1;
      residentSet.iterateClockBatch(batch, [&](PID pid) {
         u64 hash = hashPID(pid, frameCount);
         auto accessResult = ht.access<false>(pid, hash);
         BufferFrame*& bf = accessResult.first;
         u64& v = accessResult.second;
         if (bf == nullptr) {
            return;
         }
         PageState& ps = bf->state;
         switch (PageState::getState(v)) {
            case PageState::Marked: {
               if (ps.tryLockS(v)) {
                  ++evicted;
                  auto* htPage = bf->page;
                  if (htPage->isDirty()) {
                     htEntriesToWrite.push_back(pid);
                     htEntriesToWritePages.push_back(htPage);
                     htEntriesToWriteBfs.push_back(bf);
                  } else {
                     u64 v = ps.stateAndVersion.load();
                     if (PageState::getState(v) == 1 && ps.stateAndVersion.compare_exchange_weak(v, PageState::sameVersion(v, PageState::Locked))) {
                        evicted++;
                        residentSet.remove(pid);
                        ht.remove(pid, hash);
                        size_t slot = htPage - virtMem;
                        virtMemPids[slot] = std::numeric_limits<PID>::max();
                        ht.freeList.enqueue(htPage);
                        evictedHTCache.push_back(pid);
                     } else {
                        ps.unlockS();
                     }
                  }
               }

            }
               break;
            case PageState::Unlocked:
               ps.tryMark(v);
               break;
            default:
               break; // skip
         };
      });
   }

   u64 evictedFromHTCache = evictedHTCache.size();

   ioInterface[workerThreadId].writePages(htEntriesToWrite, htEntriesToWritePages);
   writeCount += htEntriesToWrite.size();

   vector<std::pair<PID, BufferFrame*>> toEvict;

   auto try_upgrade = [&](BufferFrame& bf, PID pid) {
      PageState& ps = bf.state;
      u64 v = ps.stateAndVersion;
      if ((PageState::getState(v) == 1) &&
          ps.stateAndVersion.compare_exchange_weak(v, PageState::sameVersion(v, PageState::Locked))) {
         toEvict.emplace_back(pid, &bf);
         return true;
      }
      return false;
   };

   // 3. try to upgrade lock for dirty page candidates
   for (int i = 0; i < htEntriesToWrite.size(); i++) {
      BufferFrame& bf = *htEntriesToWriteBfs[i];
      if (try_upgrade(bf, htEntriesToWrite[i])) {
         size_t slot = bf.page - virtMem;
         virtMemPids[slot] = std::numeric_limits<PID>::max();
         ht.freeList.enqueue(htEntriesToWritePages[i]);
         evictedFromHTCache++;
      } else
         bf.state.unlockS();
   }

   // 5. remove from hash table and unlock
   for (auto& [pid, ps]: toEvict) {
      bool succ = residentSet.remove(pid);
      assert(succ);
      // Search for the entry
      ht.remove(pid, hashPID(pid, frameCount));
   }

   evictsHTCache.fetch_add(evictedFromHTCache, std::memory_order_relaxed);
}

inline Page* BufferManager::getFreePageHTCache(PID pid) {
   // First try to find a free page in htcache
   while (true) {
      Page* page = nullptr;
      bool found = ht.freeList.try_dequeue(page);
      if (found) {
         if (trySetPresent(page, pid)) {
            return page;
         }
      } else {
         evict();
      }
   }
}

inline void BufferManager::flush() {
   std::vector<std::vector<PID>> pids;
   pids.emplace_back();
   std::vector<std::vector<Page*>> pages;
   pages.emplace_back();
   residentSet.iterateAll([&](PID pid) {
      BufferFrame* bf = ht.access(pid, hashPID(pid, frameCount)).first;
      if (bf == nullptr || !bf->page->dirty) {
         return;
      }
#ifdef USE_LIBAIO
      size_t maxIOs = LibaioInterface::maxIOs;
#else
      size_t maxIOs = UringInterface::maxIOs;
#endif
      if (pids.back().size() == maxIOs) {
         pages.emplace_back();
         pids.emplace_back();
      }
      pids.back().push_back(pid);
      pages.back().push_back(bf->page);
   });
   for (size_t i = 0; i < pids.size(); ++i) {
      ioInterface[0].writePages(pids[i], pages[i]);
   }

   // Verify all pages are non-dirty
   residentSet.iterateAll([&](PID pid) {
      BufferFrame* bf = ht.access(pid, hashPID(pid, frameCount)).first;
      assert(bf == nullptr || !bf->page->dirty);
   });
}

inline void BufferManager::clear() {
   flush();
   vector<PID> pids;
   residentSet.iterateAll([&](PID pid) {
      pids.push_back(pid);
   });
   // free all pages in htcache
   for (PID pid : pids) {
      BufferFrame* bf = ht.access(pid, hashPID(pid, frameCount)).first;
      assert(bf);
      size_t slot = bf->page - virtMem;
      virtMemPids[slot] = std::numeric_limits<PID>::max();
      ht.remove(pid, hashPID(pid, frameCount));
      residentSet.remove(pid);
      ht.freeList.enqueue(bf->page);
   }
}



inline bool BufferManager::decidePromotionOptimistic() {
   return (FastRandomGenerator::getRandU64() & promoteOptimisticMask) == 0;
}

inline bool BufferManager::decidePromotionShared() {
   return (FastRandomGenerator::getRandU64() & promoteSharedMask) == 0;
}

inline bool BufferManager::decidePromotionExclusive() {
   return (FastRandomGenerator::getRandU64() & promoteExclusiveMask) == 0;
}


#endif // PREDICACHE_BUFFER_MANAGER_HPP
