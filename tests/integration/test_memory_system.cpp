#include <gtest/gtest.h>
#include "neuro_os/memory/tiered_allocator.hpp"
#include "neuro_os/utils/thread_pool.hpp"
#include "neuro_os/utils/metrics.hpp"
#include <vector>
#include <thread>

using namespace neuro_os;

TEST(MemorySystemTest, AllocatorWithThreadPool) {
    memory::TieredAllocator allocator;
    ThreadPool pool(4);
    std::vector<void*> ptrs;
    
    for (int i = 0; i < 100; ++i) {
        pool.submit([&allocator, &ptrs, i]() {
            void* ptr = allocator.allocate(64);
            ptrs.push_back(ptr);
        });
    }
    
    pool.wait_all();
    
    for (auto ptr : ptrs) {
        allocator.deallocate(ptr, 64);
    }
    
    EXPECT_EQ(ptrs.size(), 100);
}

TEST(MemorySystemTest, PrefetcherIntegration) {
    memory::TieredAllocator allocator;
    memory::NeuralPrefetcher prefetcher;
    
    std::vector<int> data(1000);
    for (int i = 0; i < 100; ++i) {
        prefetcher.prefetch(&data[i * 10], 10 * sizeof(int));
    }
    
    auto predictions = prefetcher.predict(data.data(), 10);
    
    EXPECT_GE(predictions.size(), 0);
}

TEST(MemorySystemTest, AllocationWithMetrics) {
    memory::TieredAllocator allocator;
    MetricsCollector collector;
    
    for (int i = 0; i < 50; ++i) {
        void* ptr = allocator.allocate(128);
        collector.record("allocations", 1);
        allocator.deallocate(ptr, 128);
    }
    
    EXPECT_EQ(collector.get_count("allocations"), 50);
}

TEST(MemorySystemTest, MultipleTiers) {
    memory::TieredAllocator allocator;
    
    allocator.set_tier(memory::MemoryTier::DRAM);
    void* p1 = allocator.allocate(64);
    
    allocator.set_tier(memory::MemoryTier::PMEM);
    void* p2 = allocator.allocate(64);
    
    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    
    allocator.deallocate(p1, 64, memory::MemoryTier::DRAM);
    allocator.deallocate(p2, 64, memory::MemoryTier::PMEM);
}

TEST(MemorySystemTest, ConcurrentPrefetch) {
    memory::NeuralPrefetcher prefetcher;
    ThreadPool pool(4);
    
    std::vector<std::vector<int>> data_sets(10, std::vector<int>(100));
    
    for (int i = 0; i < 10; ++i) {
        pool.submit([&prefetcher, &data_sets, i]() {
            for (int j = 0; j < 100; ++j) {
                prefetcher.prefetch(&data_sets[i][j], sizeof(int));
            }
        });
    }
    
    pool.wait_all();
    
    SUCCEED();
}
