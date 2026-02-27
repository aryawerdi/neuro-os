#ifndef NEURO_OS_MEMORY_TIERED_ALLOCATOR_HPP
#define NEURO_OS_MEMORY_TIERED_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <atomic>
#include <mutex>

#include "numa_affinity.hpp"
#include "tiered_pool.hpp"

namespace neuro_os::memory {

struct AllocationMetrics {
    size_t total_allocated;
    size_t total_freed;
    size_t current_usage;
    size_t peak_usage;
    size_t allocation_count;
    size_t deallocation_count;
    uint64_t total_allocations;
    uint64_t total_deallocations;
};

struct TierConfig {
    MemoryTier tier;
    size_t max_size;
    size_t threshold;
    bool enabled;
    int numa_node;
    float priority;
};

inline TierConfig make_dram_config() {
    TierConfig config;
    config.tier = MemoryTier::DRAM;
    config.max_size = 0;
    config.threshold = 1024;
    config.enabled = true;
    config.numa_node = 0;
    config.priority = 1.0f;
    return config;
}

inline TierConfig make_pmem_config() {
    TierConfig config;
    config.tier = MemoryTier::PMEM;
    config.max_size = 0;
    config.threshold = 64 * 1024;
    config.enabled = false;
    config.numa_node = -1;
    config.priority = 0.5f;
    return config;
}

inline TierConfig make_disk_config() {
    TierConfig config;
    config.tier = MemoryTier::DISK;
    config.max_size = 0;
    config.threshold = 1024 * 1024;
    config.enabled = false;
    config.numa_node = -1;
    config.priority = 0.1f;
    return config;
}

class AllocationTracker {
private:
    struct AllocationInfo {
        void* ptr;
        size_t size;
        MemoryTier tier;
        uint64_t id;
        int numa_node;
    };

    std::unordered_map<void*, AllocationInfo> allocations_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_id_;

public:
    AllocationTracker();
    ~AllocationTracker();

    void track(void* ptr, size_t size, MemoryTier tier, int numa_node);
    void untrack(void* ptr);
    AllocationInfo* get_info(void* ptr);
    bool contains(void* ptr) const;

    size_t total_allocated() const;
    size_t allocation_count() const;
};

class TieredAllocator {
public:
    enum class Strategy {
        FIRST_FIT,
        BEST_FIT,
        WORST_FIT,
        ROUND_ROBIN,
        TIER_AFFINITY
    };

private:
    std::vector<TierConfig> tier_configs_;
    std::vector<PoolAllocator> tier_pools_;
    NUMAAffinity* numa_;
    AllocationTracker* tracker_;

    size_t small_threshold_;
    Strategy strategy_;
    int default_tier_;
    bool track_allocations_;
    bool enable_numa_;
    bool owns_numa_;
    bool owns_tracker_;

    std::atomic<size_t> total_allocated_;
    std::atomic<size_t> peak_allocated_;

    MemoryTier select_tier(size_t size) const;
    PoolAllocator& get_pool(MemoryTier tier);
    const PoolAllocator& get_pool(MemoryTier tier) const;

public:
    TieredAllocator();
    explicit TieredAllocator(const std::vector<TierConfig>& configs);
    ~TieredAllocator();

    void configure_tier(const TierConfig& config);
    void set_tier_threshold(size_t bytes, MemoryTier tier);
    void set_strategy(Strategy strategy);
    void set_small_threshold(size_t bytes);
    void enable_numa(bool enable);
    void enable_tracking(bool enable);

    void* allocate(size_t size, MemoryTier tier = MemoryTier::DRAM);
    void* allocate_optimal(size_t size);
    void* allocate_numa(size_t size, int node);

    void deallocate(void* ptr, size_t size);
    void deallocate_with_tier(void* ptr, size_t size, MemoryTier tier);

    bool owns(void* ptr) const;
    MemoryTier get_tier(void* ptr) const;

    AllocationMetrics get_metrics() const;
    void reset_metrics();

    const NUMAAffinity& numa() const { return *numa_; }
    NUMAAffinity& numa() { return *numa_; }

    size_t small_threshold() const { return small_threshold_; }
    Strategy strategy() const { return strategy_; }
    int default_tier() const { return default_tier_; }

    void trim();
    void reset();
};

inline AllocationTracker::AllocationTracker()
    : next_id_(1)
{
}

inline AllocationTracker::~AllocationTracker() = default;

inline void AllocationTracker::track(void* ptr, size_t size, MemoryTier tier, int numa_node) {
    std::lock_guard<std::mutex> lock(mutex_);

    AllocationInfo info;
    info.ptr = ptr;
    info.size = size;
    info.tier = tier;
    info.id = next_id_.fetch_add(1, std::memory_order_relaxed);
    info.numa_node = numa_node;

    allocations_[ptr] = info;
}

inline void AllocationTracker::untrack(void* ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    allocations_.erase(ptr);
}

inline AllocationTracker::AllocationInfo* AllocationTracker::get_info(void* ptr) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        return &it->second;
    }
    return nullptr;
}

inline bool AllocationTracker::contains(void* ptr) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocations_.find(ptr) != allocations_.end();
}

inline size_t AllocationTracker::total_allocated() const {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t total = 0;
    for (const auto& pair : allocations_) {
        total += pair.second.size;
    }
    return total;
}

inline size_t AllocationTracker::allocation_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocations_.size();
}

inline TieredAllocator::TieredAllocator()
    : small_threshold_(1024)
    , strategy_(Strategy::TIER_AFFINITY)
    , default_tier_(0)
    , track_allocations_(false)
    , enable_numa_(false)
    , owns_numa_(true)
    , owns_tracker_(true)
    , total_allocated_(0)
    , peak_allocated_(0)
{
    tier_configs_.push_back(make_dram_config());
    tier_configs_.push_back(make_pmem_config());
    tier_configs_.push_back(make_disk_config());

    numa_ = new NUMAAffinity();
    tracker_ = new AllocationTracker();

    tier_pools_.reserve(3);
    for (size_t i = 0; i < 3; ++i) {
        tier_pools_.push_back(PoolAllocator(small_threshold_));
    }
}

inline TieredAllocator::TieredAllocator(const std::vector<TierConfig>& configs)
    : tier_configs_(configs)
    , small_threshold_(1024)
    , strategy_(Strategy::TIER_AFFINITY)
    , default_tier_(0)
    , track_allocations_(false)
    , enable_numa_(false)
    , owns_numa_(true)
    , owns_tracker_(true)
    , total_allocated_(0)
    , peak_allocated_(0)
{
    numa_ = new NUMAAffinity();
    tracker_ = new AllocationTracker();

    tier_pools_.reserve(tier_configs_.size());
    for (size_t i = 0; i < tier_configs_.size(); ++i) {
        tier_pools_.push_back(PoolAllocator(small_threshold_));
    }
}

inline TieredAllocator::~TieredAllocator() {
    if (owns_tracker_) {
        delete tracker_;
    }
    if (owns_numa_) {
        delete numa_;
    }
}

inline void TieredAllocator::configure_tier(const TierConfig& config) {
    for (auto& cfg : tier_configs_) {
        if (cfg.tier == config.tier) {
            cfg = config;
            return;
        }
    }
    tier_configs_.push_back(config);
}

inline void TieredAllocator::set_tier_threshold(size_t bytes, MemoryTier tier) {
    for (auto& cfg : tier_configs_) {
        if (cfg.tier == tier) {
            cfg.threshold = bytes;
            return;
        }
    }
}

inline void TieredAllocator::set_strategy(Strategy strategy) {
    strategy_ = strategy;
}

inline void TieredAllocator::set_small_threshold(size_t bytes) {
    small_threshold_ = bytes;
    for (auto& pool : tier_pools_) {
        pool.set_small_threshold(bytes);
    }
}

inline void TieredAllocator::enable_numa(bool enable) {
    enable_numa_ = enable;
}

inline void TieredAllocator::enable_tracking(bool enable) {
    track_allocations_ = enable;
}

inline MemoryTier TieredAllocator::select_tier(size_t size) const {
    if (strategy_ == Strategy::TIER_AFFINITY) {
        for (const auto& cfg : tier_configs_) {
            if (cfg.enabled && size <= cfg.threshold) {
                return cfg.tier;
            }
        }
    }
    return MemoryTier::DRAM;
}

inline PoolAllocator& TieredAllocator::get_pool(MemoryTier tier) {
    if (tier == MemoryTier::DRAM) {
        return tier_pools_[0];
    }
    else if (tier == MemoryTier::PMEM) {
        return tier_pools_[1];
    }
    else if (tier == MemoryTier::DISK) {
        return tier_pools_[2];
    }
    else if (tier == MemoryTier::REMOTE) {
        return tier_pools_[0];
    }
    return tier_pools_[0];
}

inline const PoolAllocator& TieredAllocator::get_pool(MemoryTier tier) const {
    if (tier == MemoryTier::DRAM) {
        return tier_pools_[0];
    }
    else if (tier == MemoryTier::PMEM) {
        return tier_pools_[1];
    }
    else if (tier == MemoryTier::DISK) {
        return tier_pools_[2];
    }
    else if (tier == MemoryTier::REMOTE) {
        return tier_pools_[0];
    }
    return tier_pools_[0];
}

inline void* TieredAllocator::allocate(size_t size, MemoryTier tier) {
    if (size == 0) {
        return nullptr;
    }

    PoolAllocator& pool = get_pool(tier);
    void* ptr = pool.allocate(size);

    if (ptr) {
        size_t current = total_allocated_.fetch_add(size, std::memory_order_relaxed);
        size_t peak = peak_allocated_.load(std::memory_order_relaxed);
        while (current + size > peak && !peak_allocated_.compare_exchange_weak(peak, current + size)) {
        }

        if (track_allocations_) {
            int node = enable_numa_ ? numa_->get_current_node() : 0;
            tracker_->track(ptr, size, tier, node);
        }
    }

    return ptr;
}

inline void* TieredAllocator::allocate_optimal(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    MemoryTier tier = select_tier(size);
    return allocate(size, tier);
}

inline void* TieredAllocator::allocate_numa(size_t size, int node) {
    if (size == 0) {
        return nullptr;
    }

    void* ptr = nullptr;
    if (enable_numa_ && numa_) {
        ptr = numa_->allocate_on_node(size, node);
    } else {
        ptr = std::malloc(size);
    }

    if (ptr) {
        size_t current = total_allocated_.fetch_add(size, std::memory_order_relaxed);
        size_t peak = peak_allocated_.load(std::memory_order_relaxed);
        while (current + size > peak && !peak_allocated_.compare_exchange_weak(peak, current + size)) {
        }

        if (track_allocations_) {
            tracker_->track(ptr, size, MemoryTier::DRAM, node);
        }
    }

    return ptr;
}

inline void TieredAllocator::deallocate(void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return;
    }

    MemoryTier tier = MemoryTier::DRAM;
    if (track_allocations_) {
        auto* info = tracker_->get_info(ptr);
        if (info) {
            tier = info->tier;
        }
    }

    PoolAllocator& pool = get_pool(tier);
    pool.deallocate(ptr, size);

    total_allocated_.fetch_sub(size, std::memory_order_relaxed);

    if (track_allocations_) {
        tracker_->untrack(ptr);
    }
}

inline void TieredAllocator::deallocate_with_tier(void* ptr, size_t size, MemoryTier tier) {
    if (!ptr || size == 0) {
        return;
    }

    PoolAllocator& pool = get_pool(tier);
    pool.deallocate(ptr, size);

    total_allocated_.fetch_sub(size, std::memory_order_relaxed);

    if (track_allocations_) {
        tracker_->untrack(ptr);
    }
}

inline bool TieredAllocator::owns(void* ptr) const {
    if (!ptr) {
        return false;
    }

    for (const auto& pool : tier_pools_) {
        if (pool.owns(ptr)) {
            return true;
        }
    }

    if (track_allocations_) {
        return tracker_->contains(ptr);
    }

    return false;
}

inline MemoryTier TieredAllocator::get_tier(void* ptr) const {
    if (track_allocations_) {
        auto* info = const_cast<AllocationTracker*>(tracker_)->get_info(ptr);
        if (info) {
            return info->tier;
        }
    }
    return MemoryTier::DRAM;
}

inline AllocationMetrics TieredAllocator::get_metrics() const {
    AllocationMetrics metrics;
    metrics.total_allocated = total_allocated_.load(std::memory_order_relaxed);
    metrics.peak_usage = peak_allocated_.load(std::memory_order_relaxed);
    metrics.current_usage = metrics.total_allocated;
    metrics.allocation_count = 0;
    metrics.deallocation_count = 0;
    metrics.total_allocations = 0;
    metrics.total_freed = 0;
    metrics.total_deallocations = 0;

    if (track_allocations_) {
        metrics.total_allocated = tracker_->total_allocated();
        metrics.allocation_count = tracker_->allocation_count();
    }

    return metrics;
}

inline void TieredAllocator::reset_metrics() {
    total_allocated_.store(0, std::memory_order_relaxed);
    peak_allocated_.store(0, std::memory_order_relaxed);
}

inline void TieredAllocator::trim() {
    for (auto& pool : tier_pools_) {
        pool.trim_all();
    }
}

inline void TieredAllocator::reset() {
    trim();
    reset_metrics();
}

}

#endif
