#ifndef NEURO_OS_MEMORY_MEMORY_SAFETY_HPP
#define NEURO_OS_MEMORY_MEMORY_SAFETY_HPP

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

#include "tiered_allocator.hpp"

namespace neuro_os::memory {

struct BoundsCheckConfig {
    size_t red_zone_size;
    size_t guard_page_size;
    bool enable_front_guard;
    bool enable_back_guard;
    bool enable_canary;
    uint64_t canary_value;
};

struct SafetyStats {
    size_t bounds_violations;
    size_t use_after_free_detections;
    size_t double_free_detections;
    size_t memory_leak_detections;
    size_t invalid_free_detections;
    size_t total_checks;
    size_t passed_checks;
};

class BoundsChecker {
private:
    struct ProtectedBlock {
        void* user_ptr;
        void* actual_ptr;
        size_t user_size;
        size_t actual_size;
        uint64_t front_canary;
        uint64_t back_canary;
        bool is_freed;
        std::thread::id alloc_thread;
        std::chrono::steady_clock::time_point alloc_time;
    };
    
    BoundsCheckConfig config_;
    std::unordered_map<void*, ProtectedBlock> protected_blocks_;
    mutable std::mutex mutex_;
    SafetyStats stats_;
    
    void* add_protection(void* ptr, size_t size);
    void* remove_protection(void* ptr);
    bool check_canaries(const ProtectedBlock& block) const;
    bool check_guard_pages(const ProtectedBlock& block) const;
    
public:
    explicit BoundsChecker(const BoundsCheckConfig& config = BoundsCheckConfig());
    ~BoundsChecker();
    
    void* protect_allocation(void* ptr, size_t size);
    void* unprotect_allocation(void* ptr);
    
    bool check_bounds(void* ptr);
    bool check_all_bounds() const;
    
    void mark_as_freed(void* ptr);
    bool is_freed(void* ptr) const;
    
    const SafetyStats& get_stats() const { return stats_; }
    void reset_stats() { stats_ = SafetyStats(); }
    
    size_t get_protected_block_count() const;
    size_t get_total_protected_memory() const;
    
    void enable_protection(bool enable);
    bool is_protection_enabled() const;
    
    void dump_protected_blocks() const;
};

class UseAfterFreeDetector {
private:
    struct QuarantineEntry {
        void* ptr;
        size_t size;
        MemoryTier tier;
        std::chrono::steady_clock::time_point free_time;
        std::thread::id free_thread;
        uint64_t quarantine_id;
        bool is_poisoned;
    };
    
    struct DelayedFreeEntry {
        void* ptr;
        size_t size;
        MemoryTier tier;
        std::chrono::steady_clock::time_point schedule_time;
        std::chrono::steady_clock::duration delay;
    };
    
    std::vector<QuarantineEntry> quarantine_;
    std::vector<DelayedFreeEntry> delayed_frees_;
    mutable std::mutex mutex_;
    SafetyStats stats_;
    
    size_t max_quarantine_size_;
    size_t max_quarantine_memory_;
    std::chrono::steady_clock::duration quarantine_time_;
    std::chrono::steady_clock::duration default_delay_;
    
    void poison_region(void* ptr, size_t size);
    bool check_poisoned(const void* ptr, size_t size) const;
    void process_quarantine();
    void process_delayed_frees();
    
public:
    UseAfterFreeDetector(size_t max_quarantine_size = 1000,
                        size_t max_quarantine_memory = 100 * 1024 * 1024,
                        std::chrono::steady_clock::duration quarantine_time = std::chrono::seconds(30),
                        std::chrono::steady_clock::duration default_delay = std::chrono::seconds(10));
    ~UseAfterFreeDetector();
    
    void quarantine_free(void* ptr, size_t size, MemoryTier tier);
    void delayed_free(void* ptr, size_t size, MemoryTier tier, std::chrono::steady_clock::duration delay = std::chrono::steady_clock::duration::zero());
    
    bool check_use_after_free(void* ptr, size_t size);
    void record_access(void* ptr, size_t size);
    
    void process_all();
    void clear_quarantine();
    void clear_delayed_frees();
    
    const SafetyStats& get_stats() const { return stats_; }
    void reset_stats() { stats_ = SafetyStats(); }
    
    size_t get_quarantine_size() const;
    size_t get_quarantine_memory() const;
    size_t get_delayed_free_count() const;
    
    void set_quarantine_time(std::chrono::steady_clock::duration time) { quarantine_time_ = time; }
    void set_default_delay(std::chrono::steady_clock::duration delay) { default_delay_ = delay; }
    
    void dump_quarantine() const;
    void dump_delayed_frees() const;
};

class DoubleFreeDetector {
private:
    struct FreeRecord {
        void* ptr;
        size_t size;
        MemoryTier tier;
        std::chrono::steady_clock::time_point free_time;
        std::thread::id free_thread;
        uint64_t free_id;
    };
    
    struct FreeListValidation {
        void* free_list_head;
        size_t free_list_size;
        std::vector<void*> free_blocks;
        std::chrono::steady_clock::time_point validation_time;
        bool is_valid;
    };
    
    std::unordered_map<void*, FreeRecord> free_records_;
    std::vector<FreeListValidation> validations_;
    mutable std::mutex mutex_;
    SafetyStats stats_;
    std::atomic<uint64_t> next_free_id_;
    
    bool validate_free_list(void* head, size_t size);
    bool check_for_duplicates(void* ptr) const;
    
public:
    DoubleFreeDetector();
    ~DoubleFreeDetector();
    
    bool check_double_free(void* ptr, size_t size, MemoryTier tier);
    void record_free(void* ptr, size_t size, MemoryTier tier);
    
    bool validate_free_list(void* head, size_t size, const char* list_name = nullptr);
    bool validate_all_free_lists();
    
    const SafetyStats& get_stats() const { return stats_; }
    void reset_stats() { stats_ = SafetyStats(); }
    
    size_t get_free_record_count() const;
    size_t get_validation_count() const;
    size_t get_failed_validations() const;
    
    void clear_free_records();
    void clear_validations();
    
    void dump_free_records() const;
    void dump_validations() const;
};

class MemoryLeakDetector {
private:
    struct AllocationRecord {
        void* ptr;
        size_t size;
        MemoryTier tier;
        int numa_node;
        std::chrono::steady_clock::time_point alloc_time;
        std::thread::id alloc_thread;
        const char* tag;
        uint64_t allocation_id;
        bool is_active;
    };
    
    struct LeakReport {
        std::vector<AllocationRecord> leaks;
        size_t total_leaked_memory;
        size_t leak_count;
        std::chrono::steady_clock::time_point report_time;
    };
    
    std::unordered_map<void*, AllocationRecord> active_allocations_;
    std::vector<AllocationRecord> historical_allocations_;
    std::vector<LeakReport> leak_reports_;
    mutable std::mutex mutex_;
    SafetyStats stats_;
    std::atomic<uint64_t> next_allocation_id_;
    
    size_t max_active_samples_;
    size_t max_historical_samples_;
    bool enabled_;
    
public:
    MemoryLeakDetector(size_t max_active = 100000, size_t max_historical = 100000);
    ~MemoryLeakDetector();
    
    void start_tracking();
    void stop_tracking();
    
    void track_allocation(void* ptr, size_t size, MemoryTier tier, int numa_node, const char* tag = nullptr);
    void track_deallocation(void* ptr);
    
    LeakReport detect_leaks() const;
    LeakReport detect_leaks_since(std::chrono::steady_clock::time_point since) const;
    
    bool has_leaks() const;
    size_t get_leak_count() const;
    size_t get_leaked_memory() const;
    
    const SafetyStats& get_stats() const { return stats_; }
    void reset_stats() { stats_ = SafetyStats(); }
    
    size_t get_active_allocation_count() const;
    size_t get_historical_allocation_count() const;
    size_t get_report_count() const;
    
    void generate_leak_report(const std::string& filename = "");
    void dump_active_allocations() const;
    void dump_historical_allocations(size_t count = 100) const;
    
    void reset();
};

class MemorySafetyManager {
private:
    BoundsChecker* bounds_checker_;
    UseAfterFreeDetector* use_after_free_detector_;
    DoubleFreeDetector* double_free_detector_;
    MemoryLeakDetector* memory_leak_detector_;
    
    bool bounds_checking_enabled_;
    bool use_after_free_detection_enabled_;
    bool double_free_detection_enabled_;
    bool memory_leak_detection_enabled_;
    
public:
    MemorySafetyManager();
    ~MemorySafetyManager();
    
    void enable_bounds_checking(const BoundsCheckConfig& config = BoundsCheckConfig());
    void enable_use_after_free_detection(size_t max_quarantine_size = 1000,
                                        size_t max_quarantine_memory = 100 * 1024 * 1024,
                                        std::chrono::steady_clock::duration quarantine_time = std::chrono::seconds(30));
    void enable_double_free_detection();
    void enable_memory_leak_detection(size_t max_active = 100000, size_t max_historical = 100000);
    
    void disable_bounds_checking();
    void disable_use_after_free_detection();
    void disable_double_free_detection();
    void disable_memory_leak_detection();
    
    void* protect_allocation(void* ptr, size_t size, MemoryTier tier, int numa_node, const char* tag = nullptr);
    void* unprotect_allocation(void* ptr, size_t size);
    
    void record_allocation(void* ptr, size_t size, MemoryTier tier, int numa_node, const char* tag = nullptr);
    void record_deallocation(void* ptr, size_t size, MemoryTier tier);
    
    void record_access(void* ptr, size_t size);
    
    SafetyStats get_total_stats() const;
    void reset_all_stats();
    
    bool check_all_safety() const;
    void process_all_detectors();
    
    void generate_safety_report(const std::string& filename = "");
    void dump_all_state() const;
    
    bool is_bounds_checking_enabled() const { return bounds_checking_enabled_; }
    bool is_use_after_free_detection_enabled() const { return use_after_free_detection_enabled_; }
    bool is_double_free_detection_enabled() const { return double_free_detection_enabled_; }
    bool is_memory_leak_detection_enabled() const { return memory_leak_detection_enabled_; }
};

inline BoundsChecker::BoundsChecker(const BoundsCheckConfig& config)
    : config_(config)
{
    stats_ = SafetyStats();
    
    if (config_.canary_value == 0) {
        config_.canary_value = 0xDEADBEEFCAFEBABE;
    }
}

inline BoundsChecker::~BoundsChecker() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& entry : protected_blocks_) {
        std::free(entry.second.actual_ptr);
    }
    protected_blocks_.clear();
}

inline void* BoundsChecker::protect_allocation(void* ptr, size_t size) {
    if (!ptr || size == 0) return ptr;
    
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.total_checks++;
    
    void* protected_ptr = add_protection(ptr, size);
    if (protected_ptr) {
        stats_.passed_checks++;
    }
    
    return protected_ptr;
}

inline void* BoundsChecker::unprotect_allocation(void* ptr) {
    if (!ptr) return nullptr;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = protected_blocks_.find(ptr);
    if (it == protected_blocks_.end()) {
        return ptr;
    }
    
    void* user_ptr = remove_protection(ptr);
    protected_blocks_.erase(it);
    
    return user_ptr;
}

inline bool BoundsChecker::check_bounds(void* ptr) {
    if (!ptr) return true;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = protected_blocks_.find(ptr);
    if (it == protected_blocks_.end()) {
        return true;
    }
    
    const ProtectedBlock& block = it->second;
    
    if (!check_canaries(block)) {
        stats_.bounds_violations++;
        return false;
    }
    
    if (!check_guard_pages(block)) {
        stats_.bounds_violations++;
        return false;
    }
    
    return true;
}

inline void* BoundsChecker::add_protection(void* ptr, size_t size) {
    size_t front_guard = config_.enable_front_guard ? config_.red_zone_size : 0;
    size_t back_guard = config_.enable_back_guard ? config_.red_zone_size : 0;
    size_t front_canary = config_.enable_canary ? sizeof(uint64_t) : 0;
    size_t back_canary = config_.enable_canary ? sizeof(uint64_t) : 0;
    
    size_t total_size = front_guard + front_canary + size + back_canary + back_guard;
    void* actual_ptr = std::malloc(total_size);
    if (!actual_ptr) return nullptr;
    
    char* current = static_cast<char*>(actual_ptr);
    
    if (config_.enable_front_guard) {
        std::memset(current, 0xFE, front_guard);
        current += front_guard;
    }
    
    if (config_.enable_canary) {
        *reinterpret_cast<uint64_t*>(current) = config_.canary_value;
        current += sizeof(uint64_t);
    }
    
    void* user_ptr = current;
    std::memcpy(user_ptr, ptr, size);
    current += size;
    
    if (config_.enable_canary) {
        *reinterpret_cast<uint64_t*>(current) = config_.canary_value;
        current += sizeof(uint64_t);
    }
    
    if (config_.enable_back_guard) {
        std::memset(current, 0xFE, back_guard);
    }
    
    ProtectedBlock block;
    block.user_ptr = user_ptr;
    block.actual_ptr = actual_ptr;
    block.user_size = size;
    block.actual_size = total_size;
    block.front_canary = config_.enable_canary ? config_.canary_value : 0;
    block.back_canary = config_.enable_canary ? config_.canary_value : 0;
    block.is_freed = false;
    block.alloc_thread = std::this_thread::get_id();
    block.alloc_time = std::chrono::steady_clock::now();
    
    protected_blocks_[user_ptr] = block;
    std::free(ptr);
    
    return user_ptr;
}

inline void* BoundsChecker::remove_protection(void* ptr) {
    auto it = protected_blocks_.find(ptr);
    if (it == protected_blocks_.end()) {
        return ptr;
    }
    
    ProtectedBlock& block = it->second;
    
    if (!check_canaries(block) || !check_guard_pages(block)) {
        stats_.bounds_violations++;
        return nullptr;
    }
    
    void* user_ptr = std::malloc(block.user_size);
    if (!user_ptr) return nullptr;
    
    char* source = static_cast<char*>(block.actual_ptr);
    if (config_.enable_front_guard) {
        source += config_.red_zone_size;
    }
    if (config_.enable_canary) {
        source += sizeof(uint64_t);
    }
    
    std::memcpy(user_ptr, source, block.user_size);
    std::free(block.actual_ptr);
    
    return user_ptr;
}

inline bool BoundsChecker::check_canaries(const ProtectedBlock& block) const {
    if (!config_.enable_canary) return true;
    
    char* actual_ptr = static_cast<char*>(block.actual_ptr);
    char* front_canary_ptr = actual_ptr;
    if (config_.enable_front_guard) {
        front_canary_ptr += config_.red_zone_size;
    }
    
    char* back_canary_ptr = actual_ptr + block.actual_size - sizeof(uint64_t);
    if (config_.enable_back_guard) {
        back_canary_ptr -= config_.red_zone_size;
    }
    
    uint64_t front_canary = *reinterpret_cast<uint64_t*>(front_canary_ptr);
    uint64_t back_canary = *reinterpret_cast<uint64_t*>(back_canary_ptr);
    
    return front_canary == config_.canary_value && back_canary == config_.canary_value;
}

inline UseAfterFreeDetector::UseAfterFreeDetector(size_t max_quarantine_size,
                                                 size_t max_quarantine_memory,
                                                 std::chrono::steady_clock::duration quarantine_time,
                                                 std::chrono::steady_clock::duration default_delay)
    : max_quarantine_size_(max_quarantine_size)
    , max_quarantine_memory_(max_quarantine_memory)
    , quarantine_time_(quarantine_time)
    , default_delay_(default_delay)
{
    stats_ = SafetyStats();
}

inline UseAfterFreeDetector::~UseAfterFreeDetector() {
    clear_quarantine();
    clear_delayed_frees();
}

inline void UseAfterFreeDetector::quarantine_free(void* ptr, size_t size, MemoryTier tier) {
    if (!ptr || size == 0) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.total_checks++;
    
    poison_region(ptr, size);
    
    QuarantineEntry entry;
    entry.ptr = ptr;
    entry.size = size;
    entry.tier = tier;
    entry.free_time = std::chrono::steady_clock::now();
    entry.free_thread = std::this_thread::get_id();
    entry.quarantine_id = 0;
    entry.is_poisoned = true;
    
    quarantine_.push_back(entry);
    process_quarantine();
    
    stats_.passed_checks++;
}

inline void UseAfterFreeDetector::delayed_free(void* ptr, size_t size, MemoryTier tier, std::chrono::steady_clock::duration delay) {
    if (!ptr || size == 0) return;
    
    if (delay == std::chrono::steady_clock::duration::zero()) {
        delay = default_delay_;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    DelayedFreeEntry entry;
    entry.ptr = ptr;
    entry.size = size;
    entry.tier = tier;
    entry.schedule_time = std::chrono::steady_clock::now();
    entry.delay = delay;
    
    delayed_frees_.push_back(entry);
}

inline bool UseAfterFreeDetector::check_use_after_free(void* ptr, size_t size) {
    if (!ptr || size == 0) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& entry : quarantine_) {
        if (entry.ptr == ptr) {
            if (check_poisoned(ptr, size)) {
                stats_.use_after_free_detections++;
                return true;
            }
        }
    }
    
    return false;
}

inline void UseAfterFreeDetector::poison_region(void* ptr, size_t size) {
    std::memset(ptr, 0xDD, size);
}

inline bool UseAfterFreeDetector::check_poisoned(const void* ptr, size_t size) const {
    const uint8_t* data = static_cast<const uint8_t*>(ptr);
    for (size_t i = 0; i < size; ++i) {
        if (data[i] != 0xDD) {
            return false;
        }
    }
    return true;
}

inline void UseAfterFreeDetector::process_quarantine() {
    auto now = std::chrono::steady_clock::now();
    size_t total_memory = 0;
    
    for (auto it = quarantine_.begin(); it != quarantine_.end(); ) {
        total_memory += it->size;
        
        if (now - it->free_time > quarantine_time_) {
            std::free(it->ptr);
            it = quarantine_.erase(it);
        } else {
            ++it;
        }
    }
    
    while (quarantine_.size() > max_quarantine_size_ || total_memory > max_quarantine_memory_) {
        if (!quarantine_.empty()) {
            auto oldest = quarantine_.begin();
            std::free(oldest->ptr);
            quarantine_.erase(oldest);
            
            total_memory = 0;
            for (const auto& entry : quarantine_) {
                total_memory += entry.size;
            }
        }
    }
}

inline DoubleFreeDetector::DoubleFreeDetector()
    : next_free_id_(1)
{
    stats_ = SafetyStats();
}

inline DoubleFreeDetector::~DoubleFreeDetector() {
    clear_free_records();
    clear_validations();
}

inline bool DoubleFreeDetector::check_double_free(void* ptr, size_t size, MemoryTier tier) {
    if (!ptr) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.total_checks++;
    
    if (check_for_duplicates(ptr)) {
        stats_.double_free_detections++;
        return true;
    }
    
    FreeRecord record;
    record.ptr = ptr;
    record.size = size;
    record.tier = tier;
    record.free_time = std::chrono::steady_clock::now();
    record.free_thread = std::this_thread::get_id();
    record.free_id = next_free_id_.fetch_add(1, std::memory_order_relaxed);
    
    free_records_[ptr] = record;
    stats_.passed_checks++;
    
    return false;
}

inline bool DoubleFreeDetector::check_for_duplicates(void* ptr) const {
    return free_records_.find(ptr) != free_records_.end();
}

inline MemoryLeakDetector::MemoryLeakDetector(size_t max_active, size_t max_historical)
    : next_allocation_id_(1)
    , max_active_samples_(max_active)
    , max_historical_samples_(max_historical)
    , enabled_(false)
{
    stats_ = SafetyStats();
}

inline MemoryLeakDetector::~MemoryLeakDetector() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_allocations_.clear();
    historical_allocations_.clear();
    leak_reports_.clear();
}

inline void MemoryLeakDetector::start_tracking() {
    enabled_ = true;
}

inline void MemoryLeakDetector::stop_tracking() {
    enabled_ = false;
}

inline void MemoryLeakDetector::track_allocation(void* ptr, size_t size, MemoryTier tier, int numa_node, const char* tag) {
    if (!enabled_ || !ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.total_checks++;
    
    if (active_allocations_.size() >= max_active_samples_) {
        return;
    }
    
    AllocationRecord record;
    record.ptr = ptr;
    record.size = size;
    record.tier = tier;
    record.numa_node = numa_node;
    record.alloc_time = std::chrono::steady_clock::now();
    record.alloc_thread = std::this_thread::get_id();
    record.tag = tag;
    record.allocation_id = next_allocation_id_.fetch_add(1, std::memory_order_relaxed);
    record.is_active = true;
    
    active_allocations_[ptr] = record;
    stats_.passed_checks++;
}

inline void MemoryLeakDetector::track_deallocation(void* ptr) {
    if (!enabled_ || !ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_allocations_.find(ptr);
    if (it == active_allocations_.end()) {
        stats_.invalid_free_detections++;
        return;
    }
    
    AllocationRecord record = it->second;
    record.is_active = false;
    
    active_allocations_.erase(it);
    
    if (historical_allocations_.size() < max_historical_samples_) {
        historical_allocations_.push_back(record);
    }
}

inline MemorySafetyManager::MemorySafetyManager()
    : bounds_checker_(nullptr)
    , use_after_free_detector_(nullptr)
    , double_free_detector_(nullptr)
    , memory_leak_detector_(nullptr)
    , bounds_checking_enabled_(false)
    , use_after_free_detection_enabled_(false)
    , double_free_detection_enabled_(false)
    , memory_leak_detection_enabled_(false)
{
}

inline MemorySafetyManager::~MemorySafetyManager() {
    if (bounds_checker_) delete bounds_checker_;
    if (use_after_free_detector_) delete use_after_free_detector_;
    if (double_free_detector_) delete double_free_detector_;
    if (memory_leak_detector_) delete memory_leak_detector_;
}

inline void MemorySafetyManager::enable_bounds_checking(const BoundsCheckConfig& config) {
    if (!bounds_checker_) {
        bounds_checker_ = new BoundsChecker(config);
    }
    bounds_checking_enabled_ = true;
}

inline void MemorySafetyManager::enable_use_after_free_detection(size_t max_quarantine_size,
                                                                size_t max_quarantine_memory,
                                                                std::chrono::steady_clock::duration quarantine_time) {
    if (!use_after_free_detector_) {
        use_after_free_detector_ = new UseAfterFreeDetector(max_quarantine_size, max_quarantine_memory, quarantine_time);
    }
    use_after_free_detection_enabled_ = true;
}

inline void MemorySafetyManager::enable_double_free_detection() {
    if (!double_free_detector_) {
        double_free_detector_ = new DoubleFreeDetector();
    }
    double_free_detection_enabled_ = true;
}

inline void MemorySafetyManager::enable_memory_leak_detection(size_t max_active, size_t max_historical) {
    if (!memory_leak_detector_) {
        memory_leak_detector_ = new MemoryLeakDetector(max_active, max_historical);
        memory_leak_detector_->start_tracking();
    }
    memory_leak_detection_enabled_ = true;
}

inline void* MemorySafetyManager::protect_allocation(void* ptr, size_t size, MemoryTier tier, int numa_node, const char* tag) {
    void* result = ptr;
    
    if (bounds_checking_enabled_ && bounds_checker_) {
        result = bounds_checker_->protect_allocation(result, size);
    }
    
    if (memory_leak_detection_enabled_ && memory_leak_detector_) {
        memory_leak_detector_->track_allocation(result, size, tier, numa_node, tag);
    }
    
    return result;
}

inline void MemorySafetyManager::record_deallocation(void* ptr, size_t size, MemoryTier tier) {
    if (use_after_free_detection_enabled_ && use_after_free_detector_) {
        use_after_free_detector_->quarantine_free(ptr, size, tier);
    }
    
    if (double_free_detection_enabled_ && double_free_detector_) {
        if (double_free_detector_->check_double_free(ptr, size, tier)) {
            return;
        }
    }
    
    if (memory_leak_detection_enabled_ && memory_leak_detector_) {
        memory_leak_detector_->track_deallocation(ptr);
    }
}

inline SafetyStats MemorySafetyManager::get_total_stats() const {
    SafetyStats total;
    total.bounds_violations = 0;
    total.use_after_free_detections = 0;
    total.double_free_detections = 0;
    total.memory_leak_detections = 0;
    total.invalid_free_detections = 0;
    total.total_checks = 0;
    total.passed_checks = 0;
    
    if (bounds_checker_) {
        const SafetyStats& stats = bounds_checker_->get_stats();
        total.bounds_violations += stats.bounds_violations;
        total.total_checks += stats.total_checks;
        total.passed_checks += stats.passed_checks;
    }
    
    if (use_after_free_detector_) {
        const SafetyStats& stats = use_after_free_detector_->get_stats();
        total.use_after_free_detections += stats.use_after_free_detections;
        total.total_checks += stats.total_checks;
        total.passed_checks += stats.passed_checks;
    }
    
    if (double_free_detector_) {
        const SafetyStats& stats = double_free_detector_->get_stats();
        total.double_free_detections += stats.double_free_detections;
        total.total_checks += stats.total_checks;
        total.passed_checks += stats.passed_checks;
    }
    
    if (memory_leak_detector_) {
        const SafetyStats& stats = memory_leak_detector_->get_stats();
        total.memory_leak_detections += stats.memory_leak_detections;
        total.invalid_free_detections += stats.invalid_free_detections;
        total.total_checks += stats.total_checks;
        total.passed_checks += stats.passed_checks;
    }
    
    return total;
}

} // namespace neuro_os::memory

#endif // NEURO_OS_MEMORY_MEMORY_SAFETY_HPP