#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>

namespace neuro_os::memory {

enum class MemoryTier {
    DRAM,
    PMEM,
    DISK,
    REMOTE
};

struct AllocationStats {
    size_t total_allocated = 0;
    size_t total_freed = 0;
    size_t current_used = 0;
    size_t peak_used = 0;
    size_t allocation_count = 0;
    size_t deallocation_count = 0;
};

class TieredAllocator {
public:
    TieredAllocator();
    explicit TieredAllocator(size_t pool_size);
    ~TieredAllocator();
    
    void* allocate(size_t size);
    void* allocate(size_t size, MemoryTier tier);
    void* allocate_aligned(size_t size, size_t alignment);
    
    void deallocate(void* ptr, size_t size);
    void deallocate(void* ptr, size_t size, MemoryTier tier);
    
    void prefetch(const void* ptr, size_t size);
    void retire(void* ptr, size_t size);
    
    AllocationStats get_stats() const;
    void reset_stats();
    
    size_t get_total_memory() const { return total_memory_; }
    size_t get_used_memory() const;
    
    void set_tier(MemoryTier tier) { current_tier_ = tier; }
    MemoryTier get_tier() const { return current_tier_; }

private:
    void* allocate_from_pool(size_t size);
    void* allocate_from_arena(size_t size, MemoryTier tier);
    
    size_t total_memory_;
    MemoryTier current_tier_;
    std::atomic<size_t> used_memory_;
    std::atomic<size_t> peak_memory_;
    std::vector<uint8_t> memory_pool_;
    std::mutex mutex_;
    AllocationStats stats_;
};

class MemoryPool {
public:
    explicit MemoryPool(size_t block_size, size_t pool_size = 1024);
    ~MemoryPool();
    
    void* allocate();
    void deallocate(void* ptr);
    
    size_t block_size() const { return block_size_; }
    size_t total_blocks() const { return total_blocks_; }
    size_t available_blocks() const;
    
private:
    struct BlockHeader {
        bool in_use;
        BlockHeader* next;
    };
    
    size_t block_size_;
    size_t total_blocks_;
    BlockHeader* free_list_;
    std::vector<uint8_t> memory_;
    std::mutex mutex_;
};

class NeuralPrefetcher {
public:
    NeuralPrefetcher();
    ~NeuralPrefetcher();
    
    void prefetch(const void* ptr, size_t size);
    std::vector<void*> predict(const void* base, size_t count);
    void train(const std::vector<void*>& access_pattern);
    void clear();
    
    bool is_enabled() const { return enabled_; }
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }

private:
    bool enabled_;
    std::vector<void*> recent_accesses_;
    std::mutex mutex_;
};

}
