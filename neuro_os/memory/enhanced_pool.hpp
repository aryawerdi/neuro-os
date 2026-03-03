#ifndef NEURO_OS_MEMORY_ENHANCED_POOL_HPP
#define NEURO_OS_MEMORY_ENHANCED_POOL_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <thread>
#include <algorithm>
#include <functional>
#include <new>

#include "tiered_pool.hpp"

namespace neuro_os::memory {

struct SizeClassConfig {
    size_t size;
    size_t block_size;
    size_t slab_size;
    size_t magazine_capacity;
    bool thread_cache_enabled;
    bool memory_poisoning;
};

struct ThreadCacheStats {
    std::thread::id thread_id;
    size_t allocations;
    size_t deallocations;
    size_t cache_hits;
    size_t cache_misses;
    size_t current_blocks;
    size_t peak_blocks;
    size_t magazine_fills;
    size_t magazine_flushes;
};

class SizeClassAllocator {
private:
    SizeClassConfig config_;
    std::vector<void*> free_list_;
    std::atomic<size_t> free_count_;
    std::atomic<size_t> total_allocated_;
    std::atomic<size_t> total_freed_;
    mutable std::mutex mutex_;
    
    void* allocate_slab();
    void refill_free_list();
    
public:
    explicit SizeClassAllocator(const SizeClassConfig& config);
    ~SizeClassAllocator();
    
    void* allocate();
    void deallocate(void* ptr);
    
    size_t get_free_count() const { return free_count_.load(std::memory_order_relaxed); }
    size_t get_total_allocated() const { return total_allocated_.load(std::memory_order_relaxed); }
    size_t get_total_freed() const { return total_freed_.load(std::memory_order_relaxed); }
    size_t get_size() const { return config_.size; }
    
    void trim();
    void reset();
};

class ThreadCache {
private:
    struct Magazine {
        void** blocks;
        size_t capacity;
        size_t count;
        Magazine* next;
        
        Magazine(size_t cap);
        ~Magazine();
        
        bool push(void* block);
        void* pop();
        bool is_full() const { return count >= capacity; }
        bool is_empty() const { return count == 0; }
    };
    
    std::thread::id thread_id_;
    SizeClassAllocator* parent_;
    Magazine* current_magazine_;
    Magazine* spare_magazine_;
    ThreadCacheStats stats_;
    mutable std::mutex mutex_;
    
    void allocate_new_magazine();
    void flush_magazine(Magazine* magazine);
    
public:
    ThreadCache(std::thread::id tid, SizeClassAllocator* parent);
    ~ThreadCache();
    
    void* allocate();
    void deallocate(void* ptr);
    
    const ThreadCacheStats& get_stats() const { return stats_; }
    std::thread::id get_thread_id() const { return thread_id_; }
    
    void flush();
    void reset_stats();
};

class PerThreadCacheManager {
private:
    struct ThreadCacheEntry {
        std::thread::id thread_id;
        ThreadCache* cache;
        ThreadCacheEntry* next;
        
        ThreadCacheEntry(std::thread::id tid, ThreadCache* c);
        ~ThreadCacheEntry();
    };
    
    SizeClassAllocator* parent_;
    ThreadCacheEntry* cache_list_;
    mutable std::mutex mutex_;
    
    ThreadCache* get_or_create_cache(std::thread::id thread_id);
    
public:
    explicit PerThreadCacheManager(SizeClassAllocator* parent);
    ~PerThreadCacheManager();
    
    void* allocate();
    void deallocate(void* ptr);
    
    std::vector<ThreadCacheStats> get_all_stats() const;
    void flush_all();
    void reset_all_stats();
};

class MemoryPoisoner {
private:
    static const uint8_t POISON_ALLOC = 0xAA;
    static const uint8_t POISON_FREE = 0xDD;
    static const uint8_t GUARD_BYTE = 0xFE;
    
    size_t red_zone_size_;
    bool enabled_;
    
    void* add_red_zones(void* ptr, size_t size) const;
    void* remove_red_zones(void* ptr) const;
    void poison_region(void* ptr, size_t size, uint8_t value) const;
    bool check_red_zones(const void* ptr, size_t size) const;
    
public:
    MemoryPoisoner(size_t red_zone_size = 16, bool enabled = true);
    
    void* protect_allocation(void* ptr, size_t size);
    void* unprotect_allocation(void* ptr, size_t size);
    void poison_on_alloc(void* ptr, size_t size);
    void poison_on_free(void* ptr, size_t size);
    bool check_for_corruption(const void* ptr, size_t size) const;
    
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool is_enabled() const { return enabled_; }
};

class EnhancedPool {
private:
    struct SizeClassEntry {
        SizeClassConfig config;
        SizeClassAllocator* allocator;
        PerThreadCacheManager* cache_manager;
        MemoryPoisoner* poisoner;
        
        SizeClassEntry(const SizeClassConfig& cfg);
        ~SizeClassEntry();
    };
    
    std::string name_;
    std::vector<SizeClassEntry> size_classes_;
    std::unordered_map<size_t, size_t> size_to_class_index_;
    mutable std::mutex mutex_;
    
    size_t find_size_class(size_t size) const;
    void initialize_size_classes();
    
public:
    explicit EnhancedPool(const std::string& name);
    ~EnhancedPool();
    
    void add_size_class(const SizeClassConfig& config);
    
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);
    
    bool owns(void* ptr) const;
    
    std::vector<ThreadCacheStats> get_thread_cache_stats() const;
    std::vector<SizeClassConfig> get_size_class_configs() const;
    
    void trim();
    void reset();
    void flush_thread_caches();
};

class MagazineAllocator {
private:
    struct MagazineBatch {
        void** blocks;
        size_t count;
        size_t capacity;
        MagazineBatch* next;
        
        MagazineBatch(size_t cap);
        ~MagazineBatch();
        
        bool add_block(void* block);
        void* take_block();
        bool is_full() const { return count >= capacity; }
        bool is_empty() const { return count == 0; }
    };
    
    size_t block_size_;
    size_t magazine_capacity_;
    MagazineBatch* current_batch_;
    MagazineBatch* spare_batch_;
    std::atomic<size_t> total_allocated_;
    std::atomic<size_t> batch_count_;
    mutable std::mutex mutex_;
    
    void allocate_new_batch();
    void recycle_batch(MagazineBatch* batch);
    
public:
    MagazineAllocator(size_t block_size, size_t magazine_capacity = 64);
    ~MagazineAllocator();
    
    void* allocate();
    void deallocate(void* ptr);
    
    size_t get_block_size() const { return block_size_; }
    size_t get_total_allocated() const { return total_allocated_.load(std::memory_order_relaxed); }
    size_t get_batch_count() const { return batch_count_.load(std::memory_order_relaxed); }
    
    void flush();
    void reset();
};

inline SizeClassAllocator::SizeClassAllocator(const SizeClassConfig& config)
    : config_(config)
    , free_count_(0)
    , total_allocated_(0)
    , total_freed_(0)
{
    free_list_.reserve(config_.slab_size / config_.block_size);
}

inline SizeClassAllocator::~SizeClassAllocator() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (void* block : free_list_) {
        std::free(block);
    }
    free_list_.clear();
    free_count_.store(0, std::memory_order_relaxed);
}

inline void* SizeClassAllocator::allocate() {
    if (free_count_.load(std::memory_order_relaxed) == 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_list_.empty()) {
            refill_free_list();
        }
    }
    
    void* ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!free_list_.empty()) {
            ptr = free_list_.back();
            free_list_.pop_back();
            free_count_.fetch_sub(1, std::memory_order_relaxed);
        }
    }
    
    if (ptr) {
        total_allocated_.fetch_add(1, std::memory_order_relaxed);
        if (config_.memory_poisoning) {
            std::memset(ptr, 0xAA, config_.size);
        }
    }
    
    return ptr;
}

inline void SizeClassAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    
    if (config_.memory_poisoning) {
        std::memset(ptr, 0xDD, config_.size);
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        free_list_.push_back(ptr);
        free_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    total_freed_.fetch_add(1, std::memory_order_relaxed);
}

inline void SizeClassAllocator::refill_free_list() {
    void* slab = allocate_slab();
    if (!slab) return;
    
    char* current = static_cast<char*>(slab);
    size_t block_count = config_.slab_size / config_.block_size;
    
    for (size_t i = 0; i < block_count; ++i) {
        free_list_.push_back(current);
        current += config_.block_size;
    }
    
    free_count_.fetch_add(block_count, std::memory_order_relaxed);
}

inline void* SizeClassAllocator::allocate_slab() {
    void* slab = std::malloc(config_.slab_size);
    if (slab && config_.memory_poisoning) {
        std::memset(slab, 0xCC, config_.slab_size);
    }
    return slab;
}

inline ThreadCache::Magazine::Magazine(size_t cap)
    : capacity(cap)
    , count(0)
    , next(nullptr)
{
    blocks = static_cast<void**>(std::malloc(capacity * sizeof(void*)));
}

inline ThreadCache::Magazine::~Magazine() {
    if (blocks) {
        std::free(blocks);
    }
}

inline bool ThreadCache::Magazine::push(void* block) {
    if (is_full()) return false;
    blocks[count++] = block;
    return true;
}

inline void* ThreadCache::Magazine::pop() {
    if (is_empty()) return nullptr;
    return blocks[--count];
}

inline ThreadCache::ThreadCache(std::thread::id tid, SizeClassAllocator* parent)
    : thread_id_(tid)
    , parent_(parent)
    , current_magazine_(nullptr)
    , spare_magazine_(nullptr)
{
    stats_.thread_id = tid;
    stats_.allocations = 0;
    stats_.deallocations = 0;
    stats_.cache_hits = 0;
    stats_.cache_misses = 0;
    stats_.current_blocks = 0;
    stats_.peak_blocks = 0;
    stats_.magazine_fills = 0;
    stats_.magazine_flushes = 0;
}

inline ThreadCache::~ThreadCache() {
    flush();
    
    if (current_magazine_) {
        delete current_magazine_;
    }
    if (spare_magazine_) {
        delete spare_magazine_;
    }
}

inline void* ThreadCache::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!current_magazine_ || current_magazine_->is_empty()) {
        stats_.cache_misses++;
        
        if (!current_magazine_) {
            allocate_new_magazine();
        }
        
        if (current_magazine_->is_empty()) {
            for (size_t i = 0; i < parent_->get_size(); ++i) {
                void* block = parent_->allocate();
                if (!block) break;
                if (!current_magazine_->push(block)) {
                    parent_->deallocate(block);
                    break;
                }
                stats_.magazine_fills++;
            }
        }
    } else {
        stats_.cache_hits++;
    }
    
    void* block = current_magazine_->pop();
    if (block) {
        stats_.allocations++;
        stats_.current_blocks++;
        if (stats_.current_blocks > stats_.peak_blocks) {
            stats_.peak_blocks = stats_.current_blocks;
        }
    }
    
    return block;
}

inline void ThreadCache::deallocate(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!current_magazine_ || current_magazine_->is_full()) {
        if (!spare_magazine_) {
            spare_magazine_ = new Magazine(parent_->get_size());
        }
        
        if (current_magazine_ && current_magazine_->is_full()) {
            flush_magazine(current_magazine_);
            std::swap(current_magazine_, spare_magazine_);
        }
    }
    
    if (current_magazine_ && current_magazine_->push(ptr)) {
        stats_.deallocations++;
        stats_.current_blocks--;
    } else {
        parent_->deallocate(ptr);
    }
}

inline void ThreadCache::allocate_new_magazine() {
    if (!current_magazine_) {
        current_magazine_ = new Magazine(parent_->get_size());
    }
}

inline void ThreadCache::flush_magazine(Magazine* magazine) {
    if (!magazine) return;
    
    while (!magazine->is_empty()) {
        void* block = magazine->pop();
        parent_->deallocate(block);
    }
    
    stats_.magazine_flushes++;
}

inline PerThreadCacheManager::ThreadCacheEntry::ThreadCacheEntry(std::thread::id tid, ThreadCache* c)
    : thread_id(tid)
    , cache(c)
    , next(nullptr)
{
}

inline PerThreadCacheManager::ThreadCacheEntry::~ThreadCacheEntry() {
    if (cache) {
        delete cache;
    }
}

inline PerThreadCacheManager::PerThreadCacheManager(SizeClassAllocator* parent)
    : parent_(parent)
    , cache_list_(nullptr)
{
}

inline PerThreadCacheManager::~PerThreadCacheManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ThreadCacheEntry* current = cache_list_;
    while (current) {
        ThreadCacheEntry* next = current->next;
        delete current;
        current = next;
    }
    cache_list_ = nullptr;
}

inline ThreadCache* PerThreadCacheManager::get_or_create_cache(std::thread::id thread_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ThreadCacheEntry* current = cache_list_;
    while (current) {
        if (current->thread_id == thread_id) {
            return current->cache;
        }
        current = current->next;
    }
    
    ThreadCache* new_cache = new ThreadCache(thread_id, parent_);
    ThreadCacheEntry* new_entry = new ThreadCacheEntry(thread_id, new_cache);
    new_entry->next = cache_list_;
    cache_list_ = new_entry;
    
    return new_cache;
}

inline void* PerThreadCacheManager::allocate() {
    std::thread::id tid = std::this_thread::get_id();
    ThreadCache* cache = get_or_create_cache(tid);
    return cache->allocate();
}

inline void PerThreadCacheManager::deallocate(void* ptr) {
    if (!ptr) return;
    
    std::thread::id tid = std::this_thread::get_id();
    ThreadCache* cache = get_or_create_cache(tid);
    cache->deallocate(ptr);
}

inline MemoryPoisoner::MemoryPoisoner(size_t red_zone_size, bool enabled)
    : red_zone_size_(red_zone_size)
    , enabled_(enabled)
{
}

inline void* MemoryPoisoner::protect_allocation(void* ptr, size_t size) {
    if (!enabled_ || !ptr) return ptr;
    
    size_t total_size = size + 2 * red_zone_size_;
    void* protected_ptr = std::malloc(total_size);
    if (!protected_ptr) return nullptr;
    
    poison_region(protected_ptr, red_zone_size_, GUARD_BYTE);
    
    void* user_ptr = static_cast<char*>(protected_ptr) + red_zone_size_;
    std::memcpy(user_ptr, ptr, size);
    
    poison_region(static_cast<char*>(protected_ptr) + red_zone_size_ + size, red_zone_size_, GUARD_BYTE);
    
    std::free(ptr);
    return protected_ptr;
}

inline void* MemoryPoisoner::unprotect_allocation(void* ptr, size_t size) {
    if (!enabled_ || !ptr) return ptr;
    
    if (!check_red_zones(ptr, size)) {
        return nullptr;
    }
    
    void* user_ptr = static_cast<char*>(ptr) + red_zone_size_;
    void* unprotected_ptr = std::malloc(size);
    if (!unprotected_ptr) return nullptr;
    
    std::memcpy(unprotected_ptr, user_ptr, size);
    std::free(ptr);
    
    return unprotected_ptr;
}

inline void MemoryPoisoner::poison_on_alloc(void* ptr, size_t size) {
    if (!enabled_ || !ptr) return;
    poison_region(ptr, size, POISON_ALLOC);
}

inline void MemoryPoisoner::poison_on_free(void* ptr, size_t size) {
    if (!enabled_ || !ptr) return;
    poison_region(ptr, size, POISON_FREE);
}

inline bool MemoryPoisoner::check_for_corruption(const void* ptr, size_t size) const {
    if (!enabled_ || !ptr) return false;
    return !check_red_zones(ptr, size);
}

inline void MemoryPoisoner::poison_region(void* ptr, size_t size, uint8_t value) const {
    std::memset(ptr, value, size);
}

inline bool MemoryPoisoner::check_red_zones(const void* ptr, size_t size) const {
    const uint8_t* front_guard = static_cast<const uint8_t*>(ptr);
    const uint8_t* back_guard = front_guard + red_zone_size_ + size;
    
    for (size_t i = 0; i < red_zone_size_; ++i) {
        if (front_guard[i] != GUARD_BYTE || back_guard[i] != GUARD_BYTE) {
            return false;
        }
    }
    
    return true;
}

inline EnhancedPool::SizeClassEntry::SizeClassEntry(const SizeClassConfig& cfg)
    : config(cfg)
{
    allocator = new SizeClassAllocator(config);
    
    if (config.thread_cache_enabled) {
        cache_manager = new PerThreadCacheManager(allocator);
    } else {
        cache_manager = nullptr;
    }
    
    if (config.memory_poisoning) {
        poisoner = new MemoryPoisoner();
    } else {
        poisoner = nullptr;
    }
}

inline EnhancedPool::SizeClassEntry::~SizeClassEntry() {
    if (allocator) delete allocator;
    if (cache_manager) delete cache_manager;
    if (poisoner) delete poisoner;
}

inline EnhancedPool::EnhancedPool(const std::string& name)
    : name_(name)
{
    initialize_size_classes();
}

inline EnhancedPool::~EnhancedPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_classes_.clear();
    size_to_class_index_.clear();
}

inline void EnhancedPool::add_size_class(const SizeClassConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t index = size_classes_.size();
    size_classes_.push_back(SizeClassEntry(config));
    size_to_class_index_[config.size] = index;
}

inline size_t EnhancedPool::find_size_class(size_t size) const {
    auto it = size_to_class_index_.find(size);
    if (it != size_to_class_index_.end()) {
        return it->second;
    }
    
    for (size_t i = 0; i < size_classes_.size(); ++i) {
        if (size <= size_classes_[i].config.size) {
            return i;
        }
    }
    
    return size_classes_.size();
}

inline void* EnhancedPool::allocate(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t class_index = find_size_class(size);
    if (class_index >= size_classes_.size()) {
        return std::malloc(size);
    }
    
    SizeClassEntry& entry = size_classes_[class_index];
    void* ptr = nullptr;
    
    if (entry.config.thread_cache_enabled && entry.cache_manager) {
        ptr = entry.cache_manager->allocate();
    } else {
        ptr = entry.allocator->allocate();
    }
    
    if (ptr && entry.config.memory_poisoning && entry.poisoner) {
        entry.poisoner->poison_on_alloc(ptr, entry.config.size);
    }
    
    return ptr;
}

inline void EnhancedPool::deallocate(void* ptr, size_t size) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t class_index = find_size_class(size);
    if (class_index >= size_classes_.size()) {
        std::free(ptr);
        return;
    }
    
    SizeClassEntry& entry = size_classes_[class_index];
    
    if (entry.config.memory_poisoning && entry.poisoner) {
        entry.poisoner->poison_on_free(ptr, entry.config.size);
    }
    
    if (entry.config.thread_cache_enabled && entry.cache_manager) {
        entry.cache_manager->deallocate(ptr);
    } else {
        entry.allocator->deallocate(ptr);
    }
}

inline MagazineAllocator::MagazineBatch::MagazineBatch(size_t cap)
    : count(0)
    , capacity(cap)
    , next(nullptr)
{
    blocks = static_cast<void**>(std::malloc(capacity * sizeof(void*)));
}

inline MagazineAllocator::MagazineBatch::~MagazineBatch() {
    if (blocks) {
        std::free(blocks);
    }
}

inline bool MagazineAllocator::MagazineBatch::add_block(void* block) {
    if (is_full()) return false;
    blocks[count++] = block;
    return true;
}

inline void* MagazineAllocator::MagazineBatch::take_block() {
    if (is_empty()) return nullptr;
    return blocks[--count];
}

inline MagazineAllocator::MagazineAllocator(size_t block_size, size_t magazine_capacity)
    : block_size_(block_size)
    , magazine_capacity_(magazine_capacity)
    , current_batch_(nullptr)
    , spare_batch_(nullptr)
    , total_allocated_(0)
    , batch_count_(0)
{
}

inline MagazineAllocator::~MagazineAllocator() {
    flush();
    
    if (current_batch_) delete current_batch_;
    if (spare_batch_) delete spare_batch_;
}

inline void* MagazineAllocator::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!current_batch_ || current_batch_->is_empty()) {
        allocate_new_batch();
    }
    
    void* block = current_batch_->take_block();
    if (block) {
        total_allocated_.fetch_add(1, std::memory_order_relaxed);
    }
    
    return block;
}

inline void MagazineAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!current_batch_ || current_batch_->is_full()) {
        if (!spare_batch_) {
            spare_batch_ = new MagazineBatch(magazine_capacity_);
        }
        
        if (current_batch_ && current_batch_->is_full()) {
            recycle_batch(current_batch_);
            std::swap(current_batch_, spare_batch_);
        }
    }
    
    if (current_batch_) {
        current_batch_->add_block(ptr);
    } else {
        std::free(ptr);
    }
}

inline void MagazineAllocator::allocate_new_batch() {
    if (!current_batch_) {
        current_batch_ = new MagazineBatch(magazine_capacity_);
        batch_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    for (size_t i = 0; i < magazine_capacity_; ++i) {
        void* block = std::malloc(block_size_);
        if (!block || !current_batch_->add_block(block)) {
            if (block) std::free(block);
            break;
        }
    }
}

inline void MagazineAllocator::recycle_batch(MagazineBatch* batch) {
    if (!batch) return;
    
    while (!batch->is_empty()) {
        void* block = batch->take_block();
        std::free(block);
    }
}

} // namespace neuro_os::memory

#endif // NEURO_OS_MEMORY_ENHANCED_POOL_HPP