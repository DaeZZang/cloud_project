#ifndef PREDICACHE_VERSIONED_LOCK_HPP
#define PREDICACHE_VERSIONED_LOCK_HPP

struct VersionedLock {
    atomic<u64> version; // MSB is the lock bit, everything else is the version

    VersionedLock() = default;

    void init() { version.store(0, std::memory_order_release); }

    void lock() {
       u64 retryCounter = 0;
       while (true) {
          u64 v = version.load();
          if ((v >> 63) == 0) {
             if (version.compare_exchange_weak(v, v | (1ull << 63))) {
                assert(VersionedLock::isLocked(v));
                return;
             }
          }
          yield(retryCounter++);
       }
    }

    bool tryLock(u64 oldVersion) {
       assert(oldVersion >> 63 == 0);
       return version.compare_exchange_strong(oldVersion, oldVersion | (1ull << 63));
    }

    bool tryLock() {
       u64 v = version.load();
       if ((v >> 63) == 0) {
          return version.compare_exchange_strong(v, v | (1ull << 63));
       }
       return false;
    }

    void unlock() {
       assert(version.load() >> 63 == 1);
       u64 newVersion = ((version.load() << 1) >> 1) + 1;
       version.exchange(newVersion);
    }

    static bool isLocked(u64 v) { return (v >> 63) == 1; }

    void operator=(PageState&) = delete;
};

#endif //PREDICACHE_VERSIONED_LOCK_HPP
