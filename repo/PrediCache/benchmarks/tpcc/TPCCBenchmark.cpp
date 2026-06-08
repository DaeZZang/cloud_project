

#include "TPCCBenchmark.hpp"
#include "TPCCWorkload.hpp"

__thread int32_t tpcchistorycounter = 0;

void TPCCBenchmark::runInner() {

  u64 warehouseCount = datasize;

  predicacheAdapter<warehouse_t> warehouse;
  predicacheAdapter<district_t> district;
  predicacheAdapter<customer_t> customer;
  customer.tree.splitOrdered = true;
  predicacheAdapter<customer_wdl_t> customerwdl;
  predicacheAdapter<history_t> history;
  history.tree.splitOrdered = true;
  predicacheAdapter<neworder_t> neworder;
  predicacheAdapter<order_t> order;
  order.tree.splitOrdered = true;
  predicacheAdapter<order_wdc_t> order_wdc;
  predicacheAdapter<orderline_t> orderline;
  orderline.tree.splitOrdered = true;
  predicacheAdapter<item_t> item;
  item.tree.splitOrdered = true;
  predicacheAdapter<stock_t> stock;
  stock.tree.splitOrdered = true;

  TPCCWorkload<predicacheAdapter> tpcc(warehouse, district, customer, customerwdl, history, neworder, order, order_wdc, orderline, item, stock, true, warehouseCount, true);

  {
    tpcc.loadItem();
    tpcc.loadWarehouse();

    parallel_for(1, warehouseCount+1, max(nThreads, static_cast<uint64_t>(std::thread::hardware_concurrency()) / 2), [&](uint64_t worker, uint64_t begin, uint64_t end) {
      workerThreadId = worker;
      for (Integer w_id=begin; w_id<end; w_id++) {
        tpcc.loadStock(w_id);
        tpcc.loadDistrinct(w_id);
        for (Integer d_id = 1; d_id <= 10; d_id++) {
          tpcc.loadCustomer(w_id, d_id);
          tpcc.loadOrders(w_id, d_id);
        }
      }
    });
  }


  bm.resetStats();

  startStatThread();

  parallel_for(0, nThreads, nThreads, [&](uint64_t worker, uint64_t begin, uint64_t end) {
    workerThreadId = worker;
    u64 cnt = 0;
    u64 start = rdtsc();
    while (keepRunning.load()) {
      int w_id = tpcc.urand(1, warehouseCount); // wh crossing
      tpcc.tx(w_id);
      cnt++;
      u64 stop = rdtsc();
      if ((stop-start) > statDiff) {
        txProgress += cnt;
        start = stop;
        cnt = 0;
      }
    }
    txProgress += cnt;
  });

}

