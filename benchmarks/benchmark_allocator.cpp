#include <benchmark/benchmark.h>
#include "neuro_os/memory/tiered_allocator.hpp"

static void BM_Allocate(benchmark::State& state) {
    neuro_os::memory::TieredAllocator alloc;
    for (auto _ : state) {
        void* p = alloc.allocate(64);
        alloc.deallocate(p, 64);
    }
}
BENCHMARK(BM_Allocate);

static void BM_Allocate_VariousSizes(benchmark::State& state) {
    neuro_os::memory::TieredAllocator alloc;
    for (auto _ : state) {
        for (int size : {8, 16, 32, 64, 128, 256, 512, 1024}) {
            void* p = alloc.allocate(size);
            alloc.deallocate(p, size);
        }
    }
}
BENCHMARK(BM_Allocate_VariousSizes);

static void BM_MemoryPool_Allocate(benchmark::State& state) {
    neuro_os::memory::MemoryPool pool(64, 1000);
    for (auto _ : state) {
        void* p = pool.allocate();
        pool.deallocate(p);
    }
}
BENCHMARK(BM_MemoryPool_Allocate);

static void BM_ConcurrentAllocation(benchmark::State& state) {
    neuro_os::memory::TieredAllocator alloc;
    std::vector<void*> ptrs(state.range(0));
    for (auto _ : state) {
        for (int i = 0; i < state.range(0); ++i) {
            ptrs[i] = alloc.allocate(64);
        }
        for (int i = 0; i < state.range(0); ++i) {
            alloc.deallocate(ptrs[i], 64);
        }
    }
}
BENCHMARK(BM_ConcurrentAllocation)->Range(1, 1000);

static void BM_AlignedAllocation(benchmark::State& state) {
    neuro_os::memory::TieredAllocator alloc;
    for (auto _ : state) {
        void* p = alloc.allocate_aligned(64, state.range(0));
        alloc.deallocate(p, 64);
    }
}
BENCHMARK(BM_AlignedAllocation)->Range(8, 256);

static void BM_AllocationStats(benchmark::State& state) {
    neuro_os::memory::TieredAllocator alloc;
    for (auto _ : state) {
        void* p = alloc.allocate(64);
        auto stats = alloc.get_stats();
        alloc.deallocate(p, 64);
    }
}
BENCHMARK(BM_AllocationStats);

BENCHMARK_MAIN();
