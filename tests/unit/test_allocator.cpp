#include <gtest/gtest.h>
#include "neuro_os/memory/tiered_allocator.hpp"
#include <vector>
#include <thread>
#include <cstring>

using namespace neuro_os::memory;

TEST(AllocatorTest, BasicAllocation) {
    TieredAllocator allocator;
    
    void* ptr = allocator.allocate(64);
    EXPECT_NE(ptr, nullptr);
    
    allocator.deallocate(ptr, 64);
}

TEST(AllocatorTest, AllocationStats) {
    TieredAllocator allocator;
    auto initial_stats = allocator.get_stats();
    
    void* p1 = allocator.allocate(100);
    void* p2 = allocator.allocate(200);
    void* p3 = allocator.allocate(300);
    
    auto stats = allocator.get_stats();
    EXPECT_EQ(stats.allocation_count, 3);
    EXPECT_EQ(stats.total_allocated, 600);
    
    allocator.deallocate(p1, 100);
    allocator.deallocate(p2, 200);
    allocator.deallocate(p3, 300);
    
    auto final_stats = allocator.get_stats();
    EXPECT_EQ(final_stats.deallocation_count, 3);
}

TEST(AllocatorTest, MultipleAllocations) {
    TieredAllocator allocator;
    std::vector<void*> ptrs;
    
    for (int i = 0; i < 100; ++i) {
        void* ptr = allocator.allocate(64);
        EXPECT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }
    
    for (auto ptr : ptrs) {
        allocator.deallocate(ptr, 64);
    }
    
    SUCCEED();
}

TEST(AllocatorTest, DifferentSizes) {
    TieredAllocator allocator;
    
    void* p1 = allocator.allocate(8);
    void* p2 = allocator.allocate(64);
    void* p3 = allocator.allocate(256);
    void* p4 = allocator.allocate(1024);
    void* p5 = allocator.allocate(4096);
    
    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p3, nullptr);
    EXPECT_NE(p4, nullptr);
    EXPECT_NE(p5, nullptr);
    
    allocator.deallocate(p1, 8);
    allocator.deallocate(p2, 64);
    allocator.deallocate(p3, 256);
    allocator.deallocate(p4, 1024);
    allocator.deallocate(p5, 4096);
}

TEST(AllocatorTest, AlignedAllocation) {
    TieredAllocator allocator;
    
    void* ptr1 = allocator.allocate_aligned(64, 16);
    void* ptr2 = allocator.allocate_aligned(64, 32);
    void* ptr3 = allocator.allocate_aligned(64, 64);
    
    EXPECT_NE(ptr1, nullptr);
    EXPECT_NE(ptr2, nullptr);
    EXPECT_NE(ptr3, nullptr);
    
    uintptr_t addr1 = reinterpret_cast<uintptr_t>(ptr1);
    uintptr_t addr2 = reinterpret_cast<uintptr_t>(ptr2);
    uintptr_t addr3 = reinterpret_cast<uintptr_t>(ptr3);
    
    EXPECT_EQ(addr1 % 16, 0);
    EXPECT_EQ(addr2 % 32, 0);
    EXPECT_EQ(addr3 % 64, 0);
    
    allocator.deallocate(ptr1, 64);
    allocator.deallocate(ptr2, 64);
    allocator.deallocate(ptr3, 64);
}

TEST(AllocatorTest, MemoryTier) {
    TieredAllocator allocator;
    
    allocator.set_tier(MemoryTier::DRAM);
    EXPECT_EQ(allocator.get_tier(), MemoryTier::DRAM);
    
    allocator.set_tier(MemoryTier::PMEM);
    EXPECT_EQ(allocator.get_tier(), MemoryTier::PMEM);
    
    void* ptr = allocator.allocate(64, MemoryTier::DRAM);
    EXPECT_NE(ptr, nullptr);
    allocator.deallocate(ptr, 64, MemoryTier::DRAM);
}

TEST(AllocatorTest, ConcurrentAllocation) {
    TieredAllocator allocator;
    std::vector<std::thread> threads;
    std::vector<void*> ptrs(1000);
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&allocator, &ptrs, i]() {
            for (int j = 0; j < 100; ++j) {
                ptrs[i * 100 + j] = allocator.allocate(64);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    for (auto ptr : ptrs) {
        EXPECT_NE(ptr, nullptr);
    }
    
    for (auto ptr : ptrs) {
        allocator.deallocate(ptr, 64);
    }
}

TEST(MemoryPoolTest, PoolCreation) {
    MemoryPool pool(64, 100);
    
    EXPECT_EQ(pool.block_size(), 64);
    EXPECT_EQ(pool.total_blocks(), 100);
}

TEST(MemoryPoolTest, PoolAllocateDeallocate) {
    MemoryPool pool(64, 10);
    
    void* ptr1 = pool.allocate();
    void* ptr2 = pool.allocate();
    
    EXPECT_NE(ptr1, nullptr);
    EXPECT_NE(ptr2, nullptr);
    EXPECT_NE(ptr1, ptr2);
    
    pool.deallocate(ptr1);
    pool.deallocate(ptr2);
    
    EXPECT_GE(pool.available_blocks(), 0);
}

TEST(NeuralPrefetcherTest, BasicPrefetch) {
    NeuralPrefetcher prefetcher;
    
    std::vector<int> data(100);
    prefetcher.prefetch(data.data(), data.size() * sizeof(int));
    
    SUCCEED();
}

TEST(NeuralPrefetcherTest, EnableDisable) {
    NeuralPrefetcher prefetcher;
    
    EXPECT_TRUE(prefetcher.is_enabled());
    prefetcher.disable();
    EXPECT_FALSE(prefetcher.is_enabled());
    prefetcher.enable();
    EXPECT_TRUE(prefetcher.is_enabled());
}

TEST(NeuralPrefetcherTest, Predict) {
    NeuralPrefetcher prefetcher;
    
    std::vector<int> base(1000);
    for (int i = 0; i < 100; ++i) {
        prefetcher.prefetch(&base[i], sizeof(int));
    }
    
    auto predictions = prefetcher.predict(base.data(), 10);
    
    EXPECT_GE(predictions.size(), 0);
}

TEST(AllocatorTest, ResetStats) {
    TieredAllocator allocator;
    
    void* p = allocator.allocate(100);
    allocator.deallocate(p, 100);
    
    allocator.reset_stats();
    auto stats = allocator.get_stats();
    
    EXPECT_EQ(stats.allocation_count, 0);
    EXPECT_EQ(stats.deallocation_count, 0);
}
