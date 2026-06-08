#ifndef PREDICACHE_PAGE_HPP
#define PREDICACHE_PAGE_HPP

#include <atomic>
#include <cassert>
#include <cstring>
#include <immintrin.h>
#include <sys/mman.h>

//#define ADD_LOCK_DEBUG_INFO

#ifdef ADD_LOCK_DEBUG_INFO
#include <stacktrace>
#include <thread>
#include <iostream>
#endif

using namespace std;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef u64 PID; // page id type

static const u64 pageSize = 4096;

struct alignas(4096) Page {
   std::atomic<bool> dirty;
   PID pid;

   void setDirty(bool dirty) { this->dirty = dirty; }

   [[nodiscard]] inline bool isDirty() const { return dirty; }
};

static const int16_t maxWorkerThreads = 256;

#define die(msg)                                                                                                       \
   do {                                                                                                                \
      perror(msg);                                                                                                     \
      exit(EXIT_FAILURE);                                                                                              \
   } while (0)

inline uint64_t rdtsc() {
   uint32_t hi, lo;
   __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
   return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
}

// allocate memory using huge pages
inline void* allocHuge(size_t size) {
   void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   madvise(p, size, MADV_HUGEPAGE);
   return p;
}

// use when lock is not free
inline void yield(u64 counter) {
   if (counter < 1000000)
      _mm_pause();
   else
      sched_yield();
}

struct PageState {
   atomic<u64> stateAndVersion;
#ifdef ADD_LOCK_DEBUG_INFO
   std::stacktrace stacktrace;
   std::thread::id threadId;
#endif

   static const u64 Unlocked = 0;
   static const u64 MaxShared = 252;
   static const u64 Marked = 253;
   static const u64 Locked = 254;
   static const u64 Invalid = 255;

   PageState() = default;

   void init() { stateAndVersion.store(sameVersion(0, Invalid), std::memory_order_release); }

   static inline u64 sameVersion(u64 oldStateAndVersion, u64 newState) {
      return ((oldStateAndVersion << 8) >> 8) | newState << 56;
   }
   static inline u64 nextVersion(u64 oldStateAndVersion, u64 newState) {
      return (((oldStateAndVersion << 8) >> 8) + 1) | newState << 56;
   }

   bool tryLockX(u64 oldStateAndVersion) {
      assert(getState(oldStateAndVersion) == Unlocked || getState(oldStateAndVersion) > MaxShared);
      bool locked = stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, Locked));
#ifdef ADD_LOCK_DEBUG_INFO
      if (locked) {
         stacktrace = std::stacktrace::current();
         threadId = std::this_thread::get_id();
      }
#endif
      return locked;
   }

   u64 unlockX() {
      assert(getState() == Locked);
      u64 newVersion = nextVersion(stateAndVersion.load(), Unlocked);
      stateAndVersion.store(newVersion, std::memory_order_release);
      return newVersion;
   }

   void unlockXInvalid() {
      assert(getState() == Locked);
      stateAndVersion.store(nextVersion(stateAndVersion.load(), Invalid), std::memory_order_release);
   }

   void downgradeLock() {
      assert(getState() == Locked);
      stateAndVersion.store(nextVersion(stateAndVersion.load(), 1), std::memory_order_release);
   }

   bool tryLockS(u64 oldStateAndVersion) {
      u64 s = getState(oldStateAndVersion);
      if (s < MaxShared)
         return stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, s + 1));
      if (s == Marked) [[unlikely]]
         return stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, 1));
      return false;
   }

   void unlockS() {
      while (true) {
         u64 oldStateAndVersion = stateAndVersion.load();
         u64 state = getState(oldStateAndVersion);
         assert(state > 0 && state <= MaxShared);
         if (stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, state - 1)))
               [[likely]]
            return;
      }
   }

   bool tryMark(u64 oldStateAndVersion) {
      assert(getState(oldStateAndVersion) == Unlocked);
      return stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, Marked));
   }

   inline static u64 getState(u64 v) { return v >> 56; };
   inline u64 getState() { return getState(stateAndVersion.load()); }

   void operator=(PageState&) = delete;

#ifdef ADD_LOCK_DEBUG_INFO
   __attribute__((used)) void printStackTrace() {
      std::cout << stacktrace << std::endl;
   }
#endif
};

// open addressing hash table used for second chance replacement to keep track of currently-cached pages
struct ResidentPageSet {
   static const u64 empty = ~0ull;
   static const u64 tombstone = (~0ull) - 1;

   struct Entry {
      atomic<u64> pid;
   };

   Entry* ht;
   u64 count;
   u64 mask;
   atomic<u64> clockPos;
   atomic<u64> size;

   ResidentPageSet(u64 maxCount) : count(next_pow2(maxCount * 1.5)), mask(count - 1), clockPos(0) {
      ht = (Entry*) allocHuge(count * sizeof(Entry));
      memset((void*) ht, 0xFF, count * sizeof(Entry));
   }

   ~ResidentPageSet() { munmap(ht, count * sizeof(u64)); }

   u64 next_pow2(u64 x) { return 1 << (64 - __builtin_clzl(x - 1)); }

   u64 hash(u64 k) {
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
      return h;
   }

   void insert(u64 pid) {
      u64 startPos = hash(pid) & mask;
      u64 pos = startPos;
      while (true) {
         u64 curr = ht[pos].pid.load();
         assert(curr != pid);
         if ((curr == empty) || (curr == tombstone))
            if (ht[pos].pid.compare_exchange_strong(curr, pid)) {
               size++;
               return;
            }
         pos = (pos + 1) & mask;
      }
   }

   bool remove(u64 pid) {
      u64 pos = hash(pid) & mask;
      while (true) {
         u64 curr = ht[pos].pid.load();
         if (curr == empty) {
            return false;
         }

         if (curr == pid)
            if (ht[pos].pid.compare_exchange_strong(curr, tombstone)) {
               size--;
               return true;
            }
         pos = (pos + 1) & mask;
      }
   }

   template<class Fn>
   void iterateClockBatch(u64 batch, Fn fn) {
      u64 pos, newPos;
      do {
         pos = clockPos.load();
         newPos = (pos + batch) % count;
      } while (!clockPos.compare_exchange_strong(pos, newPos));

      for (u64 i = 0; i < batch; i++) {
         u64 curr = ht[pos].pid.load();
         if ((curr != tombstone) && (curr != empty))
            fn(curr);
         pos = (pos + 1) & mask;
      }
   }

   template<class Fn>
   void iterateAll(Fn fn) {
      for (u64 i = 0; i < count; i++) {
         u64 curr = ht[i].pid.load();
         if ((curr != tombstone) && (curr != empty))
            fn(curr);
      }
   }
};

#endif // PREDICACHE_PAGE_HPP
