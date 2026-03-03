#ifndef NEURO_OS_MEMORY_ADVANCED_TRACKING_HPP
#define NEURO_OS_MEMORY_ADVANCED_TRACKING_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <functional>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>
#include <utility>

#include "tiered_allocator.hpp"

namespace neuro_os::memory {

struct AllocationProfile {
    size_t size;
    MemoryTier tier;
    int numa_node;
    std::chrono::steady_clock::time_point allocation_time;
    std::chrono::steady_clock::time_point deallocation_time;
    uint64_t allocation_id;
    std::thread::id thread_id;
    const char* tag;
    bool is_active;
};

struct SizeDistribution {
    size_t size_class;
    size_t count;
    size_t total_bytes;
    size_t peak_count;
    size_t peak_bytes;
    std::chrono::steady_clock::duration total_lifetime;
    std::chrono::steady_clock::duration max_lifetime;
    std::chrono::steady_clock::duration min_lifetime;
};

struct TierMetrics {
    MemoryTier tier;
    size_t current_allocations;
    size_t current_bytes;
    size_t total_allocations;
    size_t total_bytes;
    size_t peak_allocations;
    size_t peak_bytes;
    std::chrono::steady_clock::duration total_lifetime;
    std::vector<SizeDistribution> size_distributions;
};

struct ThreadMetrics {
    std::thread::id thread_id;
    size_t current_allocations;
    size_t current_bytes;
    size_t total_allocations;
    size_t total_bytes;
    size_t peak_allocations;
    size_t peak_bytes;
    std::unordered_map<MemoryTier, size_t> tier_allocations;
    std::unordered_map<MemoryTier, size_t> tier_bytes;
};

class AllocationProfiler {
private:
    struct ProfileEntry {
        AllocationProfile profile;
        ProfileEntry* next;
        ProfileEntry* prev;
    };

    std::unordered_map<void*, ProfileEntry*> active_allocations_;
    std::vector<ProfileEntry*> completed_allocations_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_id_;
    size_t max_active_samples_;
    size_t max_completed_samples_;
    bool enabled_;
    std::chrono::steady_clock::time_point start_time_;

    ProfileEntry* allocate_entry();
    void deallocate_entry(ProfileEntry* entry);
    void add_to_completed(ProfileEntry* entry);
    void trim_completed();

public:
    AllocationProfiler(size_t max_active = 100000, size_t max_completed = 100000);
    ~AllocationProfiler();

    void start();
    void stop();
    void reset();

    void record_allocation(void* ptr, size_t size, MemoryTier tier, int numa_node, const char* tag = nullptr);
    void record_deallocation(void* ptr);

    size_t get_active_count() const;
    size_t get_total_count() const;
    size_t get_total_bytes() const;

    std::vector<AllocationProfile> get_active_allocations() const;
    std::vector<AllocationProfile> get_recent_completed(size_t count = 100) const;
    
    TierMetrics get_tier_metrics(MemoryTier tier) const;
    std::vector<TierMetrics> get_all_tier_metrics() const;
    
    ThreadMetrics get_thread_metrics(std::thread::id thread_id) const;
    std::vector<ThreadMetrics> get_all_thread_metrics() const;
    
    std::vector<SizeDistribution> get_size_distribution(size_t bucket_size = 64) const;
    
    std::string generate_report() const;
    void dump_to_file(const std::string& filename) const;
};

class MemoryUsageTracker {
private:
    struct PerThreadData {
        std::atomic<size_t> allocations;
        std::atomic<size_t> bytes;
        std::atomic<size_t> peak_allocations;
        std::atomic<size_t> peak_bytes;
    std::unordered_map<MemoryTier, std::atomic<size_t> > tier_allocations;
    std::unordered_map<MemoryTier, std::atomic<size_t> > tier_bytes;
        
        PerThreadData();
    };

    static __thread PerThreadData* thread_data_;
    std::unordered_map<std::thread::id, std::unique_ptr<PerThreadData> > all_thread_data_;
    mutable std::mutex mutex_;
    
    std::atomic<size_t> total_allocations_;
    std::atomic<size_t> total_bytes_;
    std::atomic<size_t> peak_allocations_;
    std::atomic<size_t> peak_bytes_;
    
    std::unordered_map<MemoryTier, std::atomic<size_t> > tier_total_allocations_;
    std::unordered_map<MemoryTier, std::atomic<size_t> > tier_total_bytes_;
    std::unordered_map<MemoryTier, std::atomic<size_t> > tier_peak_allocations_;
    std::unordered_map<MemoryTier, std::atomic<size_t> > tier_peak_bytes_;

    PerThreadData* get_or_create_thread_data();

public:
    MemoryUsageTracker();
    ~MemoryUsageTracker();

    void track_allocation(size_t size, MemoryTier tier);
    void track_deallocation(size_t size, MemoryTier tier);

    size_t get_current_usage() const;
    size_t get_peak_usage() const;
    size_t get_total_allocations() const;
    size_t get_total_bytes() const;

    size_t get_tier_usage(MemoryTier tier) const;
    size_t get_tier_peak_usage(MemoryTier tier) const;
    size_t get_tier_allocations(MemoryTier tier) const;
    size_t get_tier_bytes(MemoryTier tier) const;

    size_t get_thread_usage(std::thread::id thread_id) const;
    size_t get_thread_peak_usage(std::thread::id thread_id) const;

    std::vector<std::pair<std::thread::id, size_t> > get_thread_usage_sorted() const;
    std::vector<std::pair<MemoryTier, size_t> > get_tier_usage_sorted() const;

    void reset();
    void reset_peaks();
};

class AccessPatternAnalyzer {
private:
    struct AccessRecord {
        void* ptr;
        size_t size;
        std::chrono::steady_clock::time_point last_access;
        uint64_t access_count;
        uint64_t read_count;
        uint64_t write_count;
        MemoryTier current_tier;
        int current_numa_node;
        float hotness_score;
        bool is_migrating;
    };

    std::unordered_map<void*, AccessRecord> access_records_;
    mutable std::mutex mutex_;
    std::chrono::steady_clock::duration decay_interval_;
    float decay_factor_;
    size_t sample_rate_;
    size_t sample_counter_;

    void update_hotness_score(AccessRecord& record);
    void decay_scores();

public:
    AccessPatternAnalyzer(std::chrono::steady_clock::duration decay_interval = std::chrono::seconds(60),
                         float decay_factor = 0.9f,
                         size_t sample_rate = 1000);
    ~AccessPatternAnalyzer();

    void record_access(void* ptr, size_t size, bool is_write = false);
    void record_allocation(void* ptr, size_t size, MemoryTier tier, int numa_node);
    void record_deallocation(void* ptr);

    float get_hotness_score(void* ptr) const;
    std::vector<std::pair<void*, float> > get_hottest_blocks(size_t count = 10) const;
    std::vector<std::pair<void*, float> > get_coldest_blocks(size_t count = 10) const;

    bool should_promote(void* ptr, MemoryTier target_tier) const;
    bool should_demote(void* ptr, MemoryTier target_tier) const;
    
    std::vector<void*> get_candidates_for_promotion(MemoryTier target_tier, size_t count = 10) const;
    std::vector<void*> get_candidates_for_demotion(MemoryTier target_tier, size_t count = 10) const;

    void reset();
};

class MemoryDefragmenter {
private:
    struct FreeBlock {
        void* address;
        size_t size;
        FreeBlock* next;
        FreeBlock* prev;
        
        FreeBlock(void* addr, size_t sz) : address(addr), size(sz), next(nullptr), prev(nullptr) {}
    };

    struct MemoryRegion {
        void* base;
        size_t size;
        MemoryTier tier;
        int numa_node;
        FreeBlock* free_list;
        size_t total_free;
        size_t largest_free_block;
        size_t fragmentation_score;
    };

    std::vector<MemoryRegion> regions_;
    mutable std::mutex mutex_;
    size_t min_coalesce_size_;
    size_t max_defragmentation_size_;
    float fragmentation_threshold_;

    FreeBlock* find_free_block(MemoryRegion& region, size_t size);
    void coalesce_adjacent_blocks(FreeBlock* block);
    void split_block(FreeBlock* block, size_t requested_size);
    void remove_from_free_list(MemoryRegion& region, FreeBlock* block);
    void add_to_free_list(MemoryRegion& region, FreeBlock* block);
    void calculate_fragmentation_score(MemoryRegion& region);
    bool should_defragment(const MemoryRegion& region) const;

public:
    MemoryDefragmenter(size_t min_coalesce = 64, size_t max_defrag = 1024 * 1024, float frag_threshold = 0.3f);
    ~MemoryDefragmenter();

    void register_region(void* base, size_t size, MemoryTier tier, int numa_node);
    void unregister_region(void* base);

    void* allocate_from_region(void* region_base, size_t size);
    void deallocate_to_region(void* region_base, void* ptr, size_t size);

    size_t get_free_space(void* region_base) const;
    size_t get_largest_free_block(void* region_base) const;
    float get_fragmentation_score(void* region_base) const;

    bool try_coalesce(void* region_base);
    bool try_defragment(void* region_base);
    
    std::vector<std::pair<void*, float> > get_fragmentation_scores() const;
    std::vector<void*> get_regions_needing_defragmentation() const;

    void reset_region(void* region_base);
    void reset_all();
};

inline AllocationProfiler::AllocationProfiler(size_t max_active, size_t max_completed)
    : next_id_(1)
    , max_active_samples_(max_active)
    , max_completed_samples_(max_completed)
    , enabled_(false)
    , start_time_(std::chrono::steady_clock::now())
{
}

inline AllocationProfiler::~AllocationProfiler() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& entry : active_allocations_) {
        deallocate_entry(entry.second);
    }
    active_allocations_.clear();
    
    for (auto entry : completed_allocations_) {
        deallocate_entry(entry);
    }
    completed_allocations_.clear();
}

inline void AllocationProfiler::start() {
    enabled_ = true;
    start_time_ = std::chrono::steady_clock::now();
}

inline void AllocationProfiler::stop() {
    enabled_ = false;
}

inline void AllocationProfiler::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& entry : active_allocations_) {
        deallocate_entry(entry.second);
    }
    active_allocations_.clear();
    
    for (auto entry : completed_allocations_) {
        deallocate_entry(entry);
    }
    completed_allocations_.clear();
    
    next_id_.store(1, std::memory_order_relaxed);
    start_time_ = std::chrono::steady_clock::now();
}

inline void AllocationProfiler::record_allocation(void* ptr, size_t size, MemoryTier tier, int numa_node, const char* tag) {
    if (!enabled_ || !ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (active_allocations_.size() >= max_active_samples_) {
        return;
    }
    
    ProfileEntry* entry = allocate_entry();
    if (!entry) return;
    
    entry->profile.size = size;
    entry->profile.tier = tier;
    entry->profile.numa_node = numa_node;
    entry->profile.allocation_time = std::chrono::steady_clock::now();
    entry->profile.deallocation_time = std::chrono::steady_clock::time_point();
    entry->profile.allocation_id = next_id_.fetch_add(1, std::memory_order_relaxed);
    entry->profile.thread_id = std::this_thread::get_id();
    entry->profile.tag = tag;
    entry->profile.is_active = true;
    
    active_allocations_[ptr] = entry;
}

inline void AllocationProfiler::record_deallocation(void* ptr) {
    if (!enabled_ || !ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_allocations_.find(ptr);
    if (it == active_allocations_.end()) {
        return;
    }
    
    ProfileEntry* entry = it->second;
    entry->profile.deallocation_time = std::chrono::steady_clock::now();
    entry->profile.is_active = false;
    
    active_allocations_.erase(it);
    add_to_completed(entry);
}

inline size_t AllocationProfiler::get_active_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_allocations_.size();
}

inline size_t AllocationProfiler::get_total_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_allocations_.size() + completed_allocations_.size();
}

inline size_t AllocationProfiler::get_total_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t total = 0;
    for (const auto& entry : active_allocations_) {
        total += entry.second->profile.size;
    }
    
    for (const auto entry : completed_allocations_) {
        total += entry->profile.size;
    }
    
    return total;
}

inline MemoryUsageTracker::PerThreadData::PerThreadData()
    : allocations(0)
    , bytes(0)
    , peak_allocations(0)
    , peak_bytes(0)
{
    for (int i = 0; i <= static_cast<int>(MemoryTier::REMOTE); ++i) {
        tier_allocations[static_cast<MemoryTier>(i)].store(0);
        tier_bytes[static_cast<MemoryTier>(i)].store(0);
    }
}

inline MemoryUsageTracker::MemoryUsageTracker()
    : total_allocations_(0)
    , total_bytes_(0)
    , peak_allocations_(0)
    , peak_bytes_(0)
{
    for (int i = 0; i <= static_cast<int>(MemoryTier::REMOTE); ++i) {
        MemoryTier tier = static_cast<MemoryTier>(i);
        tier_total_allocations_[tier].store(0);
        tier_total_bytes_[tier].store(0);
        tier_peak_allocations_[tier].store(0);
        tier_peak_bytes_[tier].store(0);
    }
}

inline MemoryUsageTracker::~MemoryUsageTracker() = default;

inline MemoryUsageTracker::PerThreadData* MemoryUsageTracker::get_or_create_thread_data() {
    if (thread_data_) {
        return thread_data_;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto thread_id = std::this_thread::get_id();
    
    auto it = all_thread_data_.find(thread_id);
    if (it != all_thread_data_.end()) {
        thread_data_ = it->second.get();
        return thread_data_;
    }
    
    std::unique_ptr<PerThreadData> data(new PerThreadData());
    thread_data_ = data.get();
    all_thread_data_[thread_id] = std::move(data);
    return thread_data_;
}

inline void MemoryUsageTracker::track_allocation(size_t size, MemoryTier tier) {
    PerThreadData* thread_data = get_or_create_thread_data();
    
    thread_data->allocations.fetch_add(1, std::memory_order_relaxed);
    thread_data->bytes.fetch_add(size, std::memory_order_relaxed);
    
    size_t current_allocations = thread_data->allocations.load(std::memory_order_relaxed);
    size_t peak_allocations = thread_data->peak_allocations.load(std::memory_order_relaxed);
    while (current_allocations > peak_allocations && 
           !thread_data->peak_allocations.compare_exchange_weak(peak_allocations, current_allocations)) {
    }
    
    size_t current_bytes = thread_data->bytes.load(std::memory_order_relaxed);
    size_t peak_bytes = thread_data->peak_bytes.load(std::memory_order_relaxed);
    while (current_bytes > peak_bytes && 
           !thread_data->peak_bytes.compare_exchange_weak(peak_bytes, current_bytes)) {
    }
    
    thread_data->tier_allocations[tier].fetch_add(1, std::memory_order_relaxed);
    thread_data->tier_bytes[tier].fetch_add(size, std::memory_order_relaxed);
    
    total_allocations_.fetch_add(1, std::memory_order_relaxed);
    total_bytes_.fetch_add(size, std::memory_order_relaxed);
    
    size_t total_current_allocations = total_allocations_.load(std::memory_order_relaxed);
    size_t total_peak_allocations = peak_allocations_.load(std::memory_order_relaxed);
    while (total_current_allocations > total_peak_allocations && 
           !peak_allocations_.compare_exchange_weak(total_peak_allocations, total_current_allocations)) {
    }
    
    size_t total_current_bytes = total_bytes_.load(std::memory_order_relaxed);
    size_t total_peak_bytes = peak_bytes_.load(std::memory_order_relaxed);
    while (total_current_bytes > total_peak_bytes && 
           !peak_bytes_.compare_exchange_weak(total_peak_bytes, total_current_bytes)) {
    }
    
    tier_total_allocations_[tier].fetch_add(1, std::memory_order_relaxed);
    tier_total_bytes_[tier].fetch_add(size, std::memory_order_relaxed);
    
    size_t tier_current_allocations = tier_total_allocations_[tier].load(std::memory_order_relaxed);
    size_t tier_peak_allocations = tier_peak_allocations_[tier].load(std::memory_order_relaxed);
    while (tier_current_allocations > tier_peak_allocations && 
           !tier_peak_allocations_[tier].compare_exchange_weak(tier_peak_allocations, tier_current_allocations)) {
    }
    
    size_t tier_current_bytes = tier_total_bytes_[tier].load(std::memory_order_relaxed);
    size_t tier_peak_bytes = tier_peak_bytes_[tier].load(std::memory_order_relaxed);
    while (tier_current_bytes > tier_peak_bytes && 
           !tier_peak_bytes_[tier].compare_exchange_weak(tier_peak_bytes, tier_current_bytes)) {
    }
}

inline void MemoryUsageTracker::track_deallocation(size_t size, MemoryTier tier) {
    PerThreadData* thread_data = get_or_create_thread_data();
    
    thread_data->allocations.fetch_sub(1, std::memory_order_relaxed);
    thread_data->bytes.fetch_sub(size, std::memory_order_relaxed);
    
    thread_data->tier_allocations[tier].fetch_sub(1, std::memory_order_relaxed);
    thread_data->tier_bytes[tier].fetch_sub(size, std::memory_order_relaxed);
    
    total_allocations_.fetch_sub(1, std::memory_order_relaxed);
    total_bytes_.fetch_sub(size, std::memory_order_relaxed);
    
    tier_total_allocations_[tier].fetch_sub(1, std::memory_order_relaxed);
    tier_total_bytes_[tier].fetch_sub(size, std::memory_order_relaxed);
}

inline size_t MemoryUsageTracker::get_current_usage() const {
    return total_bytes_.load(std::memory_order_relaxed);
}

inline size_t MemoryUsageTracker::get_peak_usage() const {
    return peak_bytes_.load(std::memory_order_relaxed);
}

inline size_t MemoryUsageTracker::get_total_allocations() const {
    return total_allocations_.load(std::memory_order_relaxed);
}

inline size_t MemoryUsageTracker::get_total_bytes() const {
    return total_bytes_.load(std::memory_order_relaxed);
}

inline size_t MemoryUsageTracker::get_tier_usage(MemoryTier tier) const {
    auto it = tier_total_bytes_.find(tier);
    if (it != tier_total_bytes_.end()) {
        return it->second.load(std::memory_order_relaxed);
    }
    return 0;
}

inline size_t MemoryUsageTracker::get_tier_peak_usage(MemoryTier tier) const {
    auto it = tier_peak_bytes_.find(tier);
    if (it != tier_peak_bytes_.end()) {
        return it->second.load(std::memory_order_relaxed);
    }
    return 0;
}

inline AccessPatternAnalyzer::AccessPatternAnalyzer(std::chrono::steady_clock::duration decay_interval,
                                                   float decay_factor,
                                                   size_t sample_rate)
    : decay_interval_(decay_interval)
    , decay_factor_(decay_factor)
    , sample_rate_(sample_rate)
    , sample_counter_(0)
{
}

inline AccessPatternAnalyzer::~AccessPatternAnalyzer() = default;

inline void AccessPatternAnalyzer::record_access(void* ptr, size_t size, bool is_write) {
    if (!ptr || size == 0) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = access_records_.find(ptr);
    if (it == access_records_.end()) {
        return;
    }
    
    AccessRecord& record = it->second;
    record.last_access = std::chrono::steady_clock::now();
    record.access_count++;
    
    if (is_write) {
        record.write_count++;
    } else {
        record.read_count++;
    }
    
    sample_counter_++;
    if (sample_counter_ >= sample_rate_) {
        sample_counter_ = 0;
        decay_scores();
    }
    
    update_hotness_score(record);
}

inline void AccessPatternAnalyzer::record_allocation(void* ptr, size_t size, MemoryTier tier, int numa_node) {
    if (!ptr || size == 0) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    AccessRecord record;
    record.ptr = ptr;
    record.size = size;
    record.last_access = std::chrono::steady_clock::now();
    record.access_count = 1;
    record.read_count = 0;
    record.write_count = 0;
    record.current_tier = tier;
    record.current_numa_node = numa_node;
    record.hotness_score = 1.0f;
    record.is_migrating = false;
    
    access_records_[ptr] = record;
}

inline void AccessPatternAnalyzer::record_deallocation(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    access_records_.erase(ptr);
}

inline float AccessPatternAnalyzer::get_hotness_score(void* ptr) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = access_records_.find(ptr);
    if (it != access_records_.end()) {
        return it->second.hotness_score;
    }
    return 0.0f;
}

inline MemoryDefragmenter::MemoryDefragmenter(size_t min_coalesce, size_t max_defrag, float frag_threshold)
    : min_coalesce_size_(min_coalesce)
    , max_defragmentation_size_(max_defrag)
    , fragmentation_threshold_(frag_threshold)
{
}

inline MemoryDefragmenter::~MemoryDefragmenter() {
    reset_all();
}

inline void MemoryDefragmenter::register_region(void* base, size_t size, MemoryTier tier, int numa_node) {
    if (!base || size == 0) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    MemoryRegion region;
    region.base = base;
    region.size = size;
    region.tier = tier;
    region.numa_node = numa_node;
    region.total_free = size;
    region.largest_free_block = size;
    region.fragmentation_score = 0.0f;
    
    FreeBlock* initial_block = new FreeBlock(base, size);
    region.free_list = initial_block;
    
    regions_.push_back(region);
    calculate_fragmentation_score(regions_.back());
}

inline void MemoryDefragmenter::unregister_region(void* base) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto it = regions_.begin(); it != regions_.end(); ++it) {
        if (it->base == base) {
            FreeBlock* current = it->free_list;
            while (current) {
                FreeBlock* next = current->next;
                delete current;
                current = next;
            }
            regions_.erase(it);
            break;
        }
    }
}

inline void* MemoryDefragmenter::allocate_from_region(void* region_base, size_t size) {
    if (!region_base || size == 0) return nullptr;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& region : regions_) {
        if (region.base == region_base) {
            FreeBlock* block = find_free_block(region, size);
            if (!block) {
                return nullptr;
            }
            
            if (block->size > size + min_coalesce_size_) {
                split_block(block, size);
            }
            
            remove_from_free_list(region, block);
            region.total_free -= block->size;
            
            void* result = block->address;
            delete block;
            
            calculate_fragmentation_score(region);
            return result;
        }
    }
    
    return nullptr;
}

inline void MemoryDefragmenter::deallocate_to_region(void* region_base, void* ptr, size_t size) {
    if (!region_base || !ptr || size == 0) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& region : regions_) {
        if (region.base == region_base) {
            FreeBlock* new_block = new FreeBlock(ptr, size);
            add_to_free_list(region, new_block);
            region.total_free += size;
            
            coalesce_adjacent_blocks(new_block);
            calculate_fragmentation_score(region);
            break;
        }
    }
}

inline size_t MemoryDefragmenter::get_free_space(void* region_base) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& region : regions_) {
        if (region.base == region_base) {
            return region.total_free;
        }
    }
    return 0;
}

inline float MemoryDefragmenter::get_fragmentation_score(void* region_base) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& region : regions_) {
        if (region.base == region_base) {
            return region.fragmentation_score;
        }
    }
    return 1.0f;
}

inline bool MemoryDefragmenter::try_coalesce(void* region_base) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& region : regions_) {
        if (region.base == region_base) {
            FreeBlock* current = region.free_list;
            bool coalesced = false;
            
            while (current && current->next) {
                char* current_end = static_cast<char*>(current->address) + current->size;
                if (current_end == current->next->address) {
                    current->size += current->next->size;
                    
                    FreeBlock* to_delete = current->next;
                    current->next = to_delete->next;
                    if (to_delete->next) {
                        to_delete->next->prev = current;
                    }
                    
                    delete to_delete;
                    coalesced = true;
                } else {
                    current = current->next;
                }
            }
            
            if (coalesced) {
                calculate_fragmentation_score(region);
            }
            
            return coalesced;
        }
    }
    
    return false;
}

inline std::vector<void*> MemoryDefragmenter::get_regions_needing_defragmentation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<void*> result;
    for (const auto& region : regions_) {
        if (should_defragment(region)) {
            result.push_back(region.base);
        }
    }
    return result;
}

} // namespace neuro_os::memory

#endif // NEURO_OS_MEMORY_ADVANCED_TRACKING_HPP