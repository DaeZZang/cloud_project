#ifndef PREDICACHE_LIBAIO_INTERFACE_HPP
#define PREDICACHE_LIBAIO_INTERFACE_HPP

#include "page.hpp"
#include <libaio.h>
#include <vector>

// libaio interface used to write batches of pages
struct LibaioInterface {
    static const u64 maxIOs = 256;

    std::vector<int> blockfds;
    io_context_t ctx;
    iocb cb[maxIOs];
    iocb* cbPtr[maxIOs];
    io_event events[maxIOs];

    LibaioInterface(std::vector<int> blockfds) : blockfds(blockfds) {
        memset(&ctx, 0, sizeof(io_context_t));
        if (io_setup(maxIOs, &ctx) != 0)
            die("io_setup error");
    }

    ~LibaioInterface() {
       io_destroy(ctx);
    }

    void writePages(const std::vector<PID>& pids, const std::vector<Page*>& pages) {
        assert(pages.size() < maxIOs);
        for (u64 i=0; i<pages.size(); i++) {
            Page* page = pages[i];
            PID pid = pids[i];
            auto fileAndIndex = getFileAndIndex(pids[i]);
            page->setDirty(false);
            cbPtr[i] = &cb[i];
            io_prep_pwrite(cb+i, blockfds[fileAndIndex.first], page, pageSize, fileAndIndex.second*pageSize);
        }
        int cnt = io_submit(ctx, pages.size(), cbPtr);
        assert(cnt == pages.size());
        cnt = io_getevents(ctx, pages.size(), pages.size(), events, nullptr);
        assert(cnt == pages.size());
    }



    void writePage(const PID pid, Page* page) {
        size_t i = pid % maxIOs;
        page->setDirty(false);
        cbPtr[i] = &cb[i];
        auto fileAndIndex = getFileAndIndex(pid);
        io_prep_pwrite(cb+i, blockfds[fileAndIndex.first], page, pageSize, fileAndIndex.second*pageSize);
        int cnt = io_submit(ctx, 1, cbPtr + i);
        assert(cnt == 1);
        cnt = io_getevents(ctx, 1, 1, events, nullptr);
        assert(cnt == 1);
    }

    int readPage(const PID pid, Page* page) {
       size_t i = pid % maxIOs;
       cbPtr[i] = &cb[i];
       auto fileAndIndex = getFileAndIndex(pid);
       io_prep_pread(cb+i, blockfds[fileAndIndex.first], page, pageSize, fileAndIndex.second*pageSize);
       int cnt = io_submit(ctx, 1, cbPtr + i);
       assert(cnt == 1);
       cnt = io_getevents(ctx, 1, 1, events, nullptr);
       assert(cnt == 1);
       return cnt;
    }

    std::pair<size_t, size_t> getFileAndIndex(PID pid) {
        return {pid % blockfds.size(), pid / blockfds.size()};
    }
};

#endif //PREDICACHE_LIBAIO_INTERFACE_HPP
