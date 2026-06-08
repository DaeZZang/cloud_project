#ifndef PREDICACHE_BUFFER_FRAME_HPP
#define PREDICACHE_BUFFER_FRAME_HPP

#include "page.hpp"
#include <limits>
#include <atomic>

struct BufferFrame {
    PID pid;
    Page* page;
    PageState state;
    bool intendedSlot = false;

    // Moves all content except for the page state
    void moveContents(BufferFrame& other) {
       page = other.page;
       pid = other.pid;
       intendedSlot = other.intendedSlot;
       other.page = nullptr;
       other.pid = std::numeric_limits<u64>::max();
       other.intendedSlot = false;
    }
};

#endif //PREDICACHE_BUFFER_FRAME_HPP
