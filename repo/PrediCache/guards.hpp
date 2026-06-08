#ifndef PREDICACHE_GUARDS_HPP
#define PREDICACHE_GUARDS_HPP

#include "buffer_manager.hpp"

struct OLCRestartException {};

template<class T>
struct GuardO {
    PID pid;
    u64 hash;
    T* ptr;
    u64 version;
    BufferFrame* bf;
    static const u64 moved = ~0ull;
    bool allowPromotion = false;

    // constructor
    explicit GuardO(u64 pid) : pid(pid), hash(hashPID(pid, bm.frameCount)), ptr(reinterpret_cast<T*>(bm.intendedFrame(hash))) {
       assert(pid <= bm.frameCount);
       init();
    }

    template<class T2>
    GuardO(u64 pid, GuardO<T2>& parent) {
       parent.checkVersionAndRestart();
       this->pid = pid;
       this->hash = hashPID(pid, bm.frameCount);
       ptr = reinterpret_cast<T*>(bm.intendedFrame(hash));
       init();
    }

    GuardO(GuardO&& other) {
       pid = other.pid;
       ptr = other.ptr;
       hash = other.hash;
       version = other.version;
       bf = other.bf;
    }

    void init() {
       assert(pid != moved);
       if (pid == moved) {
          die("trying to get guard for moved page");
       }
       bool blockPromotion = false;
       for (u64 repeatCounter = 0;; repeatCounter++) {
          std::tie(bf, version) = bm.ht.access(pid, hash);
          // Check in case the buffer frame has been moved/evicted
          if (bf != nullptr && (PageState::getState(version) == PageState::Unlocked) & bf->intendedSlot) [[likely]] {
             return;
          }
          if (bf == nullptr) [[unlikely]] {
             bf = bm.handleFault(pid);
             if (bf != nullptr) {
                bf->state.unlockX();
                blockPromotion = true;
             }
             continue;
          }
          PageState& ps = bf->state;
          switch (PageState::getState(version)) {
             case PageState::Marked: {
                Page* page = bf->page;
                if (page == nullptr) {
                   continue;
                }
                ptr = reinterpret_cast<T*>(page);
                u64 newV = PageState::sameVersion(version, PageState::Unlocked);
                if (ps.stateAndVersion.compare_exchange_weak(version, newV)) {
                   version = newV;
                   return;
                }
                break;
             }
             case PageState::Invalid:
             case PageState::Locked:
                break;
             default: [[likely]]
                allowPromotion = !blockPromotion;
                Page* page = bf->page;
                if (page == nullptr)
                   continue;
                ptr = reinterpret_cast<T*>(page);
                return;
          }
          yield(repeatCounter);
       }
    }

    // move assignment operator
    GuardO& operator=(GuardO&& other) {
       if (pid != moved) {
          checkVersionAndRestart();
          // Note access for CBP and possibly promote.
          if (cbp::enabled) cbp::noteAccess(cbp::classOf(hash), bf && bf->intendedSlot);
          bool decide = cbp::enabled
                            ? cbp::decideForClass(cbp::classOf(hash))
                            : bm.decidePromotionOptimistic();
          if (allowPromotion && bm.ht.isInlined(bf) && decide) {
             if (cbp::enabled) cbp::noteAttempt(cbp::classOf(hash));
             bm.tryPromote(pid);
          }
       }
       pid = other.pid;
       ptr = other.ptr;
       version = other.version;
       bf = other.bf;
       hash = other.hash;
       allowPromotion = other.allowPromotion;
       other.pid = moved;
       other.ptr = nullptr;
       other.bf = nullptr;
       other.hash = 0;
       other.allowPromotion = false;
       return *this;
    }

    // assignment operator
    GuardO& operator=(const GuardO&) = delete;

    // copy constructor
    GuardO(const GuardO&) = delete;

    void checkVersionAndRestart() {
       if (pid != moved) {
          PageState& ps = bf->state;
          u64 stateAndVersion = ps.stateAndVersion.load();
          if (version == stateAndVersion) // fast path, nothing changed
             return;
          if ((stateAndVersion << 8) == (version << 8)) {
             // same version
             u64 state = PageState::getState(stateAndVersion);
             if (state <= PageState::MaxShared)
                return; // ignore shared locks
             if (state == PageState::Marked) {
                if (ps.stateAndVersion.compare_exchange_weak(
                        stateAndVersion, PageState::sameVersion(stateAndVersion, PageState::Unlocked)))
                   return; // mark cleared
             }
          }
          if (std::uncaught_exceptions() == 0)
             throw OLCRestartException();
       }
    }

    // destructor
    ~GuardO() noexcept(false) {
#ifdef COPY_STATS
       ++bm.opsCount;
#endif
       checkVersionAndRestart();
       if (pid != moved && bm.ht.isInlined(bf) && allowPromotion && bm.decidePromotionOptimistic())
          bm.tryPromote(pid);
    }

    T* operator->() {
       assert(pid != moved);
       return ptr;
    }

    void release() {
       checkVersionAndRestart();
       if (pid != moved && bm.ht.isInlined(bf) && allowPromotion && bm.decidePromotionOptimistic())
          bm.tryPromote(pid);
       pid = moved;
       ptr = nullptr;
    }
};

template<class T>
struct GuardX {
    PID pid;
    T* ptr;
    BufferFrame* bf;
    static const u64 moved = ~0ull;

    // constructor
    GuardX() : pid(moved), ptr(nullptr) {}

    // constructor
    explicit GuardX(u64 pid) : pid(pid) {
       bf = bm.fixX(pid);
       bf->page->setDirty(true);
       ptr = reinterpret_cast<T*>(bf->page);
    }

    explicit GuardX(GuardO<T>&& other) {
       assert(other.pid != moved);
       for (u64 repeatCounter = 0;; repeatCounter++) {
          PageState& ps = other.bf->state;
          bf = other.bf;
          u64 stateAndVersion = ps.stateAndVersion;
          if ((stateAndVersion << 8) != (other.version << 8))
             throw OLCRestartException();
          u64 state = PageState::getState(stateAndVersion);
          if ((state == PageState::Unlocked) || (state == PageState::Marked)) {
             if (ps.tryLockX(stateAndVersion)) {
                pid = other.pid;
                ptr = other.ptr;
                ptr->setDirty(true);
                other.pid = moved;
                other.ptr = nullptr;
                other.bf = nullptr;
                return;
             }
          }
          yield(repeatCounter);
       }
    }

    // assignment operator
    GuardX& operator=(const GuardX&) = delete;

    // move assignment operator
    GuardX& operator=(GuardX&& other) {
       if (pid != moved) {
          bm.unfixX(*bf);
       }
       pid = other.pid;
       ptr = other.ptr;
       bf = other.bf;
       other.pid = moved;
       other.ptr = nullptr;
       other.bf = nullptr;
       return *this;
    }

    // copy constructor
    GuardX(const GuardX&) = delete;

    // destructor
    ~GuardX() {
       if (pid != moved)
          bm.unfixX(*bf);
    }

    T* operator->() {
       assert(pid != moved);
       return ptr;
    }

    void release() {
       if (pid != moved && bm.isValidPtr(ptr)) {
          bm.unfixX(*bf);
          pid = moved;
       }
    }
};

template<class T>
struct AllocGuard : public GuardX<T> {
    template<typename... Params>
    AllocGuard(Params&&... params) {
       BufferFrame* bf = bm.allocPage();
       GuardX<T>::ptr = reinterpret_cast<T*>(bf->page);
       new (GuardX<T>::ptr) T(std::forward<Params>(params)...);
       GuardX<T>::pid = bf->pid;
       GuardX<T>::bf = bf;
    }
};

template<class T>
struct GuardS {
    PID pid;
    BufferFrame* bf;
    T* ptr;
    static const u64 moved = ~0ull;

    // constructor
    explicit GuardS(u64 pid) : pid(pid) {
       bf = bm.fixS(pid);
       ptr = reinterpret_cast<T*>(bf->page);
    }

    GuardS(GuardO<T>&& other) {
       assert(other.pid != moved);
       PageState& pageState = other.bf->state;
       if (pageState.tryLockS(other.version)) {
          // XXX: optimize?
          pid = other.pid;
          ptr = other.ptr;
          bf = other.bf;
          other.pid = moved;
          other.ptr = nullptr;
          other.bf = nullptr;
       } else {
          throw OLCRestartException();
       }
    }

    GuardS(GuardS&& other) {
       if (pid != moved) {
          bm.unfixS(*bf);
       }
       pid = other.pid;
       ptr = other.ptr;
       bf = other.bf;
       other.pid = moved;
       other.ptr = nullptr;
       other.bf = nullptr;
    }

    // assignment operator
    GuardS& operator=(const GuardS&) = delete;

    // move assignment operator
    GuardS& operator=(GuardS&& other) {
       if (pid != moved) {
          bm.unfixS(*bf);
       }
       pid = other.pid;
       ptr = other.ptr;
       bf = other.bf;
       other.pid = moved;
       other.ptr = nullptr;
       other.bf = nullptr;
       return *this;
    }

    // copy constructor
    GuardS(const GuardS&) = delete;

    // destructor
    ~GuardS() {
       if (pid != moved) {
          bm.unfixS(*bf);
       }
    }

    T* operator->() {
       assert(pid != moved);
       return ptr;
    }

    void release() {
       if (pid != moved) {
          bm.unfixS(*bf);
          pid = moved;
       }
    }
};

#endif //PREDICACHE_GUARDS_HPP
