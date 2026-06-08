#ifndef PREDICACHE_HT_HPP
#define PREDICACHE_HT_HPP

#include "buffer_frame.hpp"
#include "versioned_lock.hpp"
#include "third_party/concurrentqueue.hpp"
#include "third_party/primes.hpp"

#include <vector>

#include "third_party/libdivide.h"
#include "utils/RandomGenerator.hpp"

// Chaining Hash table with first element inlined and synchronized by PageState hybrid locks
struct HTBufferManager {
    struct Entry {
        BufferFrame bf;
        // Only modify when holding the bucket lock
        Entry* next = nullptr;

        Entry(PID pid, Page* page, Entry* next) : bf{pid, page, {}}, next(next) {
           bf.state.init();
        }
    };

    struct ChainHead {
        VersionedLock lock; // we reuse our PageState "lock". Marking and Eviction will not be used here
        Entry head{std::numeric_limits<u64>::max(), nullptr, nullptr};

        ChainHead() {
           lock.init();
           head.bf.state.init();
        }
    };

    std::atomic<u64> increases;
    std::atomic<u64> decreases;

    alignas(64) moodycamel::ConcurrentQueue<Page*> freeList;

   // We just put our "deleted" entries here so we don't get use after free
    alignas(64) moodycamel::ConcurrentQueue<Entry*> entryFreeList;

    alignas(64) ChainHead* ht;
    u64 count;
    libdivide::divider<u64> countDivider;

    HTBufferManager(u64 count) : freeList{count},
                                                          entryFreeList{count},
                                                          count(count), countDivider(count) {
       ht = static_cast<ChainHead*>(allocHuge(count * sizeof(ChainHead)));

       for (u64 i = 0; i < count; i++) {
          new (ht + i) ChainHead();
       }
    }

    ~HTBufferManager() {
       for (u64 i = 0; i < count; i++) {
          ChainHead& ch = ht[i];
          Entry* cur = ch.head.next;
          while (cur != nullptr) {
             Entry* next = cur->next;
             delete cur;
             cur = next;
          }
       }
       munmap(ht, count * sizeof(ChainHead));
    }

   bool isInlined(BufferFrame* bf) {
       return bf >= reinterpret_cast<BufferFrame*>(ht) && bf < reinterpret_cast<BufferFrame*>(ht + count);
    }

    // Returns a locked buffer frame with the page inserted
    BufferFrame* insert(PID pid, u64 hash) {
       ChainHead& bucket = ht[index(hash)];
       for (int retryCounter = 0;; retryCounter++) {
          u64 v = bucket.lock.version;
          // First we try to lookup if the page is already in the cache
          if (VersionedLock::isLocked(v))
             goto restart;
          {
             Entry* cur = &bucket.head;
             while (cur != nullptr) {
                if (cur->bf.pid == pid) {
                   if (bucket.lock.version.load() != v)
                      goto restart;
                   return nullptr;
                }
                cur = cur->next;
                if (bucket.lock.version != v)
                   goto restart;
             }
          }
          if (bucket.lock.tryLock(v)) {
             {
                Entry* cur = &bucket.head;
                while (cur != nullptr) {
                   assert(cur->bf.pid != pid);
                   cur = cur->next;
                }
             }
             if (bucket.head.bf.pid == std::numeric_limits<u64>::max()) {
                bucket.head.bf.pid = pid;
                auto v = bucket.head.bf.state.stateAndVersion.load();
                assert(PageState::getState(v) == PageState::Invalid);
                auto succ = bucket.head.bf.state.tryLockX(v);
                assert(succ);
                bucket.lock.unlock();
                assert(bucket.head.bf.state.getState() == PageState::Locked);
                return &bucket.head.bf;
             }
             Entry* entry = nullptr;
             if (entryFreeList.try_dequeue(entry)) {
                entry->bf.pid = pid;
                entry->next = bucket.head.next;
             } else {
                entry = new Entry{pid, nullptr, bucket.head.next};
             }
             bucket.head.next = entry;
             auto v = entry->bf.state.stateAndVersion.load();
             assert(PageState::getState(v) == PageState::Invalid);
             auto succ = entry->bf.state.tryLockX(v);
             assert(succ);
             bucket.lock.unlock();
             assert(entry->bf.state.getState() == PageState::Locked);
             return &entry->bf;
          }
          restart:
          yield(retryCounter);
       }
    }

    // This requires the page in question to be locked exclusively, unlocks and frees the entry
    bool remove(PID pid, u64 hash) {
       auto& bucket = ht[index(hash)];
       for (int retryCounter = 0;; retryCounter++) {
          u64 v = bucket.lock.version;
          if (!VersionedLock::isLocked(v) && bucket.lock.tryLock(v)) {
             if (bucket.head.bf.pid == pid) {
                assert(bucket.head.bf.state.getState() == PageState::Locked);
                if (false && bucket.head.next != nullptr) {
                   // We try to exclusively lock the next page and move it to the head. Otherwise we will leave the head
                   // as is, being empty
                   auto v2 = bucket.head.next->bf.state.stateAndVersion.load();
                   u64 stateBefore = PageState::getState(v2);
                   if (stateBefore == PageState::Unlocked && bucket.head.next->bf.state.tryLockX(v2)) {
                      Entry* next = bucket.head.next;
                      bucket.head.bf.moveContents(next->bf);
                      bucket.head.next = next->next;
                      next->bf.state.unlockXInvalid();
                      entryFreeList.enqueue(next);
                      bucket.head.bf.state.unlockX();
                   } else {
                      bucket.head.bf.pid = std::numeric_limits<u64>::max();
                      bucket.head.bf.page = nullptr;
                      bucket.head.bf.state.unlockXInvalid();
                   }
                   bucket.lock.unlock();
                } else {
                   bucket.head.bf.pid = std::numeric_limits<u64>::max();
                   bucket.head.bf.page = nullptr;
                   bucket.head.bf.state.unlockXInvalid();
                   bucket.lock.unlock();
                }
                return true;
             }
             Entry** cur = &bucket.head.next;
             while (*cur != nullptr) {
                if ((*cur)->bf.pid == pid) {
                   break;
                }
                cur = &(*cur)->next;
             }
             if (*cur == nullptr) {
                bucket.lock.unlock();
                return false;
             }
             assert((*cur)->bf.state.getState() == PageState::Locked);
             Entry* entry = *cur;
             entry->bf.pid = std::numeric_limits<u64>::max();
             entry->bf.page = nullptr;
             *cur = entry->next;
             bucket.lock.unlock();
             entry->bf.state.unlockXInvalid();
             entryFreeList.enqueue(entry);
             return true;
          }
          yield(retryCounter);
       }
    }

   template <bool move = true>
    std::pair<BufferFrame*, u64> access(PID pid, u64 hash) {
       auto& bucket = ht[index(hash)];
       for (int retryCounter = 0;; retryCounter++) {
          u64 vBucket = bucket.lock.version.load(std::memory_order_acquire);
          if (!VersionedLock::isLocked(vBucket)) [[likely]] {
             if (bucket.head.bf.pid == pid) [[likely]] {
                BufferFrame* bf = &bucket.head.bf;
                u64 v = bf->state.stateAndVersion.load(std::memory_order_relaxed); // Can be relaxed because optimistic bucket latch locks us in
                if (bucket.lock.version == vBucket) [[likely]] {
                   return {bf, v};
                }
             } else {
                Entry* prev = &bucket.head;
                Entry* cur = bucket.head.next;
                if (bucket.lock.version.load() != vBucket) {
                   goto restart;
                }
                while (cur != nullptr) {
                   if (cur->bf.pid == pid) {
                      break;
                   }
                   prev = cur;
                   cur = cur->next;
                   if (bucket.lock.version.load() != vBucket) {
                      goto restart;
                   }
                }
                if (cur == nullptr)
                   return {nullptr, 0};
                u64 vEntry = cur->bf.state.stateAndVersion.load();
                u64 stateBefore = PageState::getState(vEntry);
                if (bucket.lock.version == vBucket) {
                   // We have a chain but no head so we try to move the first element of the chain to the head
                   if (move && bucket.head.bf.pid == std::numeric_limits<u64>::max()
                         && FastRandomGenerator::getRandU64() & 7 == 0 && bucket.lock.tryLock(vBucket)) {
                      // Try to lock the headers lock
                      auto vHeader = bucket.head.bf.state.stateAndVersion.load();
                      [[maybe_unused]] auto succ = bucket.head.bf.state.tryLockX(vHeader);
                      assert(succ);
                      // Try to lock the found element
                      if (stateBefore == PageState::Unlocked && cur->bf.state.tryLockX(vEntry)) {
                         // Move the first element of the chain to the head
                         bucket.head.bf.moveContents(cur->bf);
                         prev->next = cur->next;
                         cur->bf.state.unlockXInvalid();
                         entryFreeList.enqueue(cur);
                         bucket.head.bf.state.unlockX();
                         u64 v = bucket.head.bf.state.stateAndVersion.load(std::memory_order_relaxed);
                         bucket.lock.unlock();
                         return {&bucket.head.bf, v};
                      }
                      bucket.head.bf.state.unlockXInvalid();
                      bucket.lock.unlock();
                   }
                   return {&cur->bf, vEntry};
                }
             }
          }
          restart:
          yield(retryCounter);
       }
    }

    u64 next_pow2(u64 x) { return 1 << (64 - __builtin_clzl(x - 1)); }

    size_t index(u64 hash) { return (hash - hash / countDivider * count); }
};

#endif //PREDICACHE_HT_HPP
