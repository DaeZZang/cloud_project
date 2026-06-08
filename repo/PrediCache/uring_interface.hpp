#ifndef PREDICACHE_URING_INTERFACE_HPP
#define PREDICACHE_URING_INTERFACE_HPP

#include <liburing.h>
#include "page.hpp"

// #define USE_POSIX_IO

// io_uring interface used to write batches of pages
struct UringInterface {
   static const u64 maxIOs = 1024;

   std::vector<int> blockfds;
   io_uring ring;

   UringInterface(std::vector<int> blockfds) : blockfds(std::move(blockfds)) {
#ifndef USE_POSIX_IO
      if (io_uring_queue_init(maxIOs, &ring, 0) < 0) {
         die("io_uring_queue_init error");
      }
#endif
   }

   ~UringInterface() {
#ifndef USE_POSIX_IO
      io_uring_queue_exit(&ring);
#endif
   }

   void writePages(const vector<PID>& pids, const vector<Page*>& pages) {
#ifndef USE_POSIX_IO
      assert(pages.size() <= maxIOs);
      for (u64 i = 0; i < pages.size(); i++) {
         Page* page = pages[i];
         PID pid = pids[i];
         page->setDirty(false);
         struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
         if (!sqe) {
            die("io_uring_get_sqe failed");
         }
         auto fileAndIndex = getFileAndIndex(pid);
         io_uring_prep_write(sqe, blockfds[fileAndIndex.first], page,
                            pageSize, pageSize * fileAndIndex.second);
      }
      int submitted = io_uring_submit(&ring);
      if (submitted < 0) {
         die("io_uring_submit error");
      }
      assert(submitted == static_cast<int>(pages.size()));
      int pending = submitted;
      while (pending > 0) {
         struct io_uring_cqe* cqe;
         int ret = io_uring_wait_cqe(&ring, &cqe);
         if (ret < 0) {
            die("io_uring_wait_cqe error");
         }
         io_uring_cqe_seen(&ring, cqe);
         pending--;
      }
#else
      for (int i = 0; i < pids.size(); i++) {
         writePage(pids[i], pages[i]);
      }
#endif
   }


   void writePage(const PID pid, Page* page) {
      auto fileAndIndex = getFileAndIndex(pid);
#ifndef USE_POSIX_IO
      page->setDirty(false);
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      if (!sqe) {
         die("io_uring_get_sqe failed");
      }
      io_uring_prep_write(sqe, blockfds[fileAndIndex.first], page,
                         pageSize, pageSize * fileAndIndex.second);
      int submitted = io_uring_submit(&ring);
      if (submitted < 0) {
         die("io_uring_submit error");
      }
      assert(submitted == 1);
      struct io_uring_cqe* cqe;
      int ret = io_uring_wait_cqe(&ring, &cqe);
      if (ret < 0) {
         die("io_uring_wait_cqe error");
      }
      io_uring_cqe_seen(&ring, cqe);
#else
      page->setDirty(false);
      u64 written = 0;
      while (written < pageSize) {
         int result = pwrite(blockfds[fileAndIndex.first], page + written, pageSize - written, pageSize * fileAndIndex.second + written);
         if (result < 0)
            die("write error");
         written += result;
      }
#endif
   }

   int readPage(const PID pid, Page* page) {
      auto fileAndIndex = getFileAndIndex(pid);
#ifndef USE_POSIX_IO
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      if (!sqe) {
         die("io_uring_get_sqe failed");
      }
      io_uring_prep_read(sqe, blockfds[fileAndIndex.first], page,
                        pageSize, pageSize * fileAndIndex.second);
      int submitted = io_uring_submit(&ring);
      if (submitted < 0) {
         die("io_uring_submit error");
      }
      assert(submitted == 1);
      struct io_uring_cqe* cqe;
      int ret = io_uring_wait_cqe(&ring, &cqe);
      if (ret < 0) {
         die("io_uring_wait_cqe error");
      }
      io_uring_cqe_seen(&ring, cqe);
      return 1;
#else
      return pread(blockfds[fileAndIndex.first], page, pageSize, pageSize * fileAndIndex.second) > 0;
#endif
   }

   void madvDontneed(std::vector<Page*>& pages) {
#ifndef USE_POSIX_IO
      if (pages.empty())
         return;
      for (auto page : pages) {
         io_uring_sqe* sqe = io_uring_get_sqe(&ring);
         if (!sqe) {
            perror("io_uring_get_sqe");
            exit(EXIT_FAILURE);
         }

         // Prepare the madvise system call
         io_uring_prep_madvise(sqe, page, pageSize, MADV_DONTNEED);
      }

      // Submit the batch
      if (io_uring_submit(&ring) < 0) {
         perror("io_uring_submit");
         exit(EXIT_FAILURE);
      }

      // Wait for completions
      for (size_t i = 0; i < pages.size(); i++) {
         io_uring_cqe* cqe;
         if (io_uring_wait_cqe(&ring, &cqe) < 0) {
            perror("io_uring_wait_cqe");
            exit(EXIT_FAILURE);
         }

         // Check for errors
         if (cqe->res < 0) {
            fprintf(stderr, "madvise failed: %s\n", strerror(-cqe->res));
         }

         io_uring_cqe_seen(&ring, cqe);
      }
#else
      for (auto page : pages) {
         madvise(page, pageSize, MADV_DONTNEED);
      }
#endif
   }

   std::pair<size_t, size_t> getFileAndIndex(PID pid) {
      return {pid % blockfds.size(), pid / blockfds.size()};
   }
};

#endif // PREDICACHE_URING_INTERFACE_HPP
