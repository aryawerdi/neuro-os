#ifndef NEURO_OS_MEMORY_TIERED_POOL_HPP
#define NEURO_OS_MEMORY_TIERED_POOL_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <list>
#include <atomic>
#include <mutex>
#include <optional>
#include <new>

namespace neuro_os::memory {

struct PoolConfig {
    size_t block_size;
    size_t pool_size;
    size_t alignment;
    bool thread_safe;
    bool zero_memory;
};

struct MemoryBlock {
    void* address;
    size_t size;
    bool in_use;
    uint64_t allocation_id;
};

struct SlabHeader {
    void* memory;
    size_t size;
    size_t used;
    size_t capacity;
    int tier;
};

class FreeList {
public:
    struct FreeNode {
        FreeNode* next;
    };

private:
    FreeNode* head_;
    size_t block_size_;
    size_t count_;
    std::atomic<size_t> available_;

public:
    FreeList();
    explicit FreeList(size_t block_size);
    ~FreeList();

    void initialize(void* memory, size_t size, size_t block_size);

    void* allocate();
    void deallocate(void* ptr);

    bool is_empty() const { return head_ == nullptr; }
    size_t available() const { return available_.load(std::memory_order_relaxed); }
    size_t block_size() const { return block_size_; }
    size_t count() const { return count_; }

    void reset();
};

class SlabAllocator {
private:
    std::vector<SlabHeader> slabs_;
    size_t block_size_;
    size_t slab_size_;
    int tier_;
    std::mutex mutex_;

    SlabHeader* allocate_slab();
    void* allocate_from_slab(SlabHeader& slab);
    void deallocate_to_slab(SlabHeader& slab, void* ptr);

public:
    SlabAllocator(size_t block_size, size_t slab_size = 4096, int tier = 0);
    ~SlabAllocator();

    void* allocate();
    void deallocate(void* ptr);

    size_t total_memory() const;
    size_t used_memory() const;
    size_t free_memory() const;

    size_t block_size() const { return block_size_; }
    int tier() const { return tier_; }

    void reset();
};

class TieredPool {
public:
    struct Stats {
        size_t total_allocated;
        size_t total_free;
        size_t active_allocations;
        size_t peak_allocations;
        size_t slab_count;
        size_t slab_memory;
    };

private:
    std::string name_;
    PoolConfig config_;
    int tier_;

    std::vector<SlabAllocator*> slabs_;
    std::vector<FreeList*> free_lists_;

    std::atomic<uint64_t> allocation_counter_;
    std::atomic<size_t> active_allocations_;
    std::atomic<size_t> peak_allocations_;

    FreeList* select_free_list(size_t size) const;
    SlabAllocator* select_slab(size_t size) const;

public:
    TieredPool(const std::string& name, const PoolConfig& config, int tier = 0);
    ~TieredPool();

    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);

    bool owns(void* ptr) const;

    Stats get_stats() const;
    void reset_stats();

    const std::string& name() const { return name_; }
    size_t block_size() const { return config_.block_size; }
    int tier() const { return tier_; }

    void trim();
    void reset();
};

class PoolAllocator {
public:
    struct PoolHandle {
        TieredPool* pool;
        size_t size;
    };

private:
    std::vector<PoolHandle> pools_;
    size_t small_threshold_;
    int default_tier_;

    TieredPool* find_pool(size_t size) const;

public:
    PoolAllocator();
    explicit PoolAllocator(size_t small_threshold);
    ~PoolAllocator();

    void add_pool(const std::string& name, const PoolConfig& config, int tier = 0);

    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);

    bool owns(void* ptr) const;

    size_t small_threshold() const { return small_threshold_; }
    void set_small_threshold(size_t threshold) { small_threshold_ = threshold; }

    std::vector<TieredPool::Stats> get_all_stats() const;
    void trim_all();
    void reset_all();
};

inline FreeList::FreeList()
    : head_(nullptr)
    , block_size_(0)
    , count_(0)
    , available_(0)
{
}

inline FreeList::FreeList(size_t block_size)
    : head_(nullptr)
    , block_size_(block_size)
    , count_(0)
    , available_(0)
{
}

inline FreeList::~FreeList() = default;

inline void FreeList::initialize(void* memory, size_t size, size_t block_size) {
    block_size_ = block_size;
    count_ = size / block_size;
    available_.store(count_, std::memory_order_relaxed);

    char* ptr = static_cast<char*>(memory);
    head_ = nullptr;

    for (size_t i = 0; i < count_; ++i) {
        FreeNode* node = reinterpret_cast<FreeNode*>(ptr + i * block_size);
        node->next = head_;
        head_ = node;
    }
}

inline void* FreeList::allocate() {
    if (!head_) {
        return nullptr;
    }

    FreeNode* node = head_;
    head_ = node->next;
    available_.fetch_sub(1, std::memory_order_relaxed);
    return node;
}

inline void FreeList::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }

    FreeNode* node = static_cast<FreeNode*>(ptr);
    node->next = head_;
    head_ = node;
    available_.fetch_add(1, std::memory_order_relaxed);
}

inline void FreeList::reset() {
    head_ = nullptr;
    available_.store(0, std::memory_order_relaxed);
}

inline SlabAllocator::SlabAllocator(size_t block_size, size_t slab_size, int tier)
    : block_size_(block_size)
    , slab_size_(slab_size)
    , tier_(tier)
{
}

inline SlabAllocator::~SlabAllocator() {
    for (auto& slab : slabs_) {
        if (slab.memory) {
            std::free(slab.memory);
        }
    }
}

inline SlabHeader* SlabAllocator::allocate_slab() {
    void* memory = std::malloc(slab_size_);
    if (!memory) {
        return nullptr;
    }

    SlabHeader slab;
    slab.memory = memory;
    slab.size = slab_size_;
    slab.used = 0;
    slab.capacity = slab_size_ / block_size_;
    slab.tier = tier_;

    slabs_.push_back(slab);
    return &slabs_.back();
}

inline void* SlabAllocator::allocate_from_slab(SlabHeader& slab) {
    if (slab.used >= slab.capacity) {
        return nullptr;
    }

    char* ptr = static_cast<char*>(slab.memory) + slab.used * block_size_;
    slab.used++;
    return ptr;
}

inline void SlabAllocator::deallocate_to_slab(SlabHeader& slab, void* ptr) {
    (void)slab;
    (void)ptr;
}

inline void* SlabAllocator::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& slab : slabs_) {
        if (slab.used < slab.capacity) {
            return allocate_from_slab(slab);
        }
    }

    SlabHeader* slab = allocate_slab();
    if (!slab) {
        return nullptr;
    }

    return allocate_from_slab(*slab);
}

inline void SlabAllocator::deallocate(void* ptr) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& slab : slabs_) {
        char* slab_start = static_cast<char*>(slab.memory);
        char* slab_end = slab_start + slab.size;
        char* ptr_char = static_cast<char*>(ptr);

        if (ptr_char >= slab_start && ptr_char < slab_end) {
            deallocate_to_slab(slab, ptr);
            return;
        }
    }
}

inline size_t SlabAllocator::total_memory() const {
    return slabs_.size() * slab_size_;
}

inline size_t SlabAllocator::used_memory() const {
    size_t used = 0;
    for (const auto& slab : slabs_) {
        used += slab.used * block_size_;
    }
    return used;
}

inline size_t SlabAllocator::free_memory() const {
    return total_memory() - used_memory();
}

inline void SlabAllocator::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& slab : slabs_) {
        slab.used = 0;
    }
}

inline TieredPool::TieredPool(const std::string& name, const PoolConfig& config, int tier)
    : name_(name)
    , config_(config)
    , tier_(tier)
    , allocation_counter_(0)
    , active_allocations_(0)
    , peak_allocations_(0)
{
    if (config_.block_size > 0 && config_.pool_size > 0) {
        slabs_.push_back(new SlabAllocator(config_.block_size, config_.pool_size, tier));
    }
}

inline TieredPool::~TieredPool() {
    for (auto slab : slabs_) {
        delete slab;
    }
}

inline void* TieredPool::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    if (size <= config_.block_size && !slabs_.empty()) {
        SlabAllocator* slab = slabs_[0];
        if (slab) {
            void* ptr = slab->allocate();
            if (ptr) {
                uint64_t id = allocation_counter_.fetch_add(1, std::memory_order_relaxed);
                (void)id;

                size_t current = active_allocations_.fetch_add(1, std::memory_order_relaxed);
                size_t peak = peak_allocations_.load(std::memory_order_relaxed);
                while (current > peak && !peak_allocations_.compare_exchange_weak(peak, current)) {
                }

                if (config_.zero_memory && ptr) {
                    std::memset(ptr, 0, config_.block_size);
                }
                return ptr;
            }
        }
    }

    void* ptr = std::malloc(size);
    if (ptr && config_.zero_memory) {
        std::memset(ptr, 0, size);
    }
    return ptr;
}

inline void TieredPool::deallocate(void* ptr, size_t size) {
    if (!ptr) {
        return;
    }

    if (size <= config_.block_size && !slabs_.empty()) {
        SlabAllocator* slab = slabs_[0];
        if (slab) {
            slab->deallocate(ptr);
        }
    } else {
        std::free(ptr);
    }

    active_allocations_.fetch_sub(1, std::memory_order_relaxed);
}

inline bool TieredPool::owns(void* ptr) const {
    (void)ptr;
    return true;
}

inline TieredPool::Stats TieredPool::get_stats() const {
    Stats stats;
    stats.total_allocated = 0;
    stats.total_free = 0;
    stats.active_allocations = active_allocations_.load(std::memory_order_relaxed);
    stats.peak_allocations = peak_allocations_.load(std::memory_order_relaxed);
    stats.slab_count = slabs_.size();
    stats.slab_memory = 0;

    for (auto slab : slabs_) {
        if (slab) {
            stats.total_allocated += slab->used_memory();
            stats.slab_memory += slab->total_memory();
        }
    }

    stats.total_free = stats.slab_memory - stats.total_allocated;
    return stats;
}

inline void TieredPool::reset_stats() {
    peak_allocations_.store(0, std::memory_order_relaxed);
}

inline void TieredPool::trim() {
    for (auto slab : slabs_) {
        if (slab) {
            slab->reset();
        }
    }
}

inline void TieredPool::reset() {
    trim();
    active_allocations_.store(0, std::memory_order_relaxed);
    allocation_counter_.store(0, std::memory_order_relaxed);
}

inline FreeList* TieredPool::select_free_list(size_t size) const {
    (void)size;
    if (!free_lists_.empty()) {
        return free_lists_[0];
    }
    return nullptr;
}

inline SlabAllocator* TieredPool::select_slab(size_t size) const {
    (void)size;
    if (!slabs_.empty()) {
        return slabs_[0];
    }
    return nullptr;
}

inline PoolAllocator::PoolAllocator()
    : small_threshold_(1024)
    , default_tier_(0)
{
}

inline PoolAllocator::PoolAllocator(size_t small_threshold)
    : small_threshold_(small_threshold)
    , default_tier_(0)
{
}

inline PoolAllocator::~PoolAllocator() = default;

inline void PoolAllocator::add_pool(const std::string& name, const PoolConfig& config, int tier) {
    TieredPool* pool = new TieredPool(name, config, tier);
    PoolHandle handle;
    handle.pool = pool;
    handle.size = config.block_size;
    pools_.push_back(handle);
}

inline void* PoolAllocator::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    TieredPool* pool = find_pool(size);
    if (pool) {
        return pool->allocate(size);
    }

    return std::malloc(size);
}

inline void PoolAllocator::deallocate(void* ptr, size_t size) {
    if (!ptr) {
        return;
    }

    TieredPool* pool = find_pool(size);
    if (pool) {
        pool->deallocate(ptr, size);
    } else {
        std::free(ptr);
    }
}

inline bool PoolAllocator::owns(void* ptr) const {
    (void)ptr;
    return true;
}

inline TieredPool* PoolAllocator::find_pool(size_t size) const {
    for (const auto& handle : pools_) {
        if (size <= handle.size) {
            return handle.pool;
        }
    }
    return nullptr;
}

inline std::vector<TieredPool::Stats> PoolAllocator::get_all_stats() const {
    std::vector<TieredPool::Stats> stats;
    stats.reserve(pools_.size());

    for (const auto& handle : pools_) {
        if (handle.pool) {
            stats.push_back(handle.pool->get_stats());
        }
    }

    return stats;
}

inline void PoolAllocator::trim_all() {
    for (const auto& handle : pools_) {
        if (handle.pool) {
            handle.pool->trim();
        }
    }
}

inline void PoolAllocator::reset_all() {
    for (const auto& handle : pools_) {
        if (handle.pool) {
            handle.pool->reset();
        }
    }
}

}

#endif
