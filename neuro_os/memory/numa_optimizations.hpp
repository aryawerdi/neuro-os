#ifndef NEURO_OS_MEMORY_NUMA_OPTIMIZATIONS_HPP
#define NEURO_OS_MEMORY_NUMA_OPTIMIZATIONS_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <thread>
#include <algorithm>
#include <functional>

#include "numa_affinity.hpp"

#if defined(__linux__) && !defined(__ANDROID__)
#include <numa.h>
#include <numaif.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace neuro_os::memory {

class HwlocIntegration {
private:
#if defined(__linux__) && !defined(__ANDROID__)
    hwloc_topology_t topology_;
#endif
    bool initialized_;
    int node_count_;
    std::vector<int> cpu_to_node_;
    std::vector<std::vector<int> > node_to_cpus_;
    
    void detect_topology();
    
public:
    HwlocIntegration();
    ~HwlocIntegration();
    
    bool is_initialized() const { return initialized_; }
    int get_node_count() const { return node_count_; }
    
    int get_node_for_cpu(int cpu) const;
    std::vector<int> get_cpus_for_node(int node) const;
    int get_best_node_for_thread(std::thread::id tid) const;
    
    size_t get_node_memory(int node) const;
    size_t get_node_free_memory(int node) const;
    
    void bind_thread_to_node(std::thread::id tid, int node);
    void bind_current_thread_to_node(int node);
    
    std::vector<int> get_interleaved_nodes(size_t size, size_t granularity = 4096) const;
    int get_optimal_node_for_size(size_t size) const;
};

class AutomaticNUMADetection {
private:
    struct NodeMetrics {
        int node_id;
        size_t total_memory;
        size_t free_memory;
        size_t allocated_memory;
        size_t access_count;
        float access_latency;
        float bandwidth;
        bool is_preferred;
    };
    
    HwlocIntegration hwloc_;
    std::vector<NodeMetrics> node_metrics_;
    std::atomic<int> preferred_node_;
    mutable std::mutex mutex_;
    int node_count_;
    
    void update_node_metrics();
    void detect_pmem_nodes();
    void detect_remote_nodes();
    
public:
    AutomaticNUMADetection();
    ~AutomaticNUMADetection();
    
    bool initialize();
    
    int get_preferred_node() const { return preferred_node_.load(std::memory_order_relaxed); }
    void set_preferred_node(int node) { preferred_node_.store(node, std::memory_order_relaxed); }
    
    int get_optimal_node_for_allocation(size_t size, bool prefer_local = true) const;
    std::vector<int> get_nodes_by_memory_availability() const;
    std::vector<int> get_nodes_by_performance() const;
    
    size_t get_total_system_memory() const;
    size_t get_total_free_memory() const;
    size_t get_node_memory_usage(int node) const;
    
    void record_allocation(int node, size_t size);
    void record_deallocation(int node, size_t size);
    void record_access(int node, size_t size, bool is_local);
    
    std::vector<NodeMetrics> get_node_metrics() const;
};

class InterleavedAllocator {
private:
    struct InterleavedRegion {
        void* base;
        size_t size;
        std::vector<int> nodes;
        size_t granularity;
        size_t allocated;
        size_t max_allocations;
    };
    
    std::vector<InterleavedRegion> regions_;
    mutable std::mutex mutex_;
    size_t default_granularity_;
    bool use_huge_pages_;
    
    void* allocate_interleaved_region(size_t size, const std::vector<int>& nodes, size_t granularity);
    void deallocate_interleaved_region(void* ptr);
    int get_node_for_offset(void* ptr, size_t offset, const InterleavedRegion& region) const;
    
public:
    InterleavedAllocator(size_t default_granularity = 4096, bool use_huge_pages = false);
    ~InterleavedAllocator();
    
    void* allocate(size_t size, const std::vector<int>& nodes, size_t granularity = 0);
    void* allocate_auto_interleaved(size_t size);
    void deallocate(void* ptr);
    
    bool is_interleaved(void* ptr) const;
    std::vector<int> get_nodes_for_region(void* ptr) const;
    size_t get_granularity_for_region(void* ptr) const;
    
    size_t get_total_interleaved_memory() const;
    size_t get_allocated_interleaved_memory() const;
    
    void enable_huge_pages(bool enable) { use_huge_pages_ = enable; }
    bool is_huge_pages_enabled() const { return use_huge_pages_; }
    
    void reset();
};

class MemoryMigration {
private:
    struct MigrationTask {
        void* address;
        size_t size;
        int source_node;
        int target_node;
        std::chrono::steady_clock::time_point scheduled_time;
        float priority;
        bool completed;
        bool failed;
        
        bool operator<(const MigrationTask& other) const {
            return priority < other.priority;
        }
    };
    
    std::vector<MigrationTask> pending_migrations_;
    std::vector<MigrationTask> completed_migrations_;
    mutable std::mutex mutex_;
    std::atomic<bool> migration_enabled_;
    size_t max_concurrent_migrations_;
    size_t migration_batch_size_;
    
    bool migrate_pages_linux(void* ptr, size_t size, int target_node);
    bool migrate_pages_portable(void* ptr, size_t size, int target_node);
    
    float calculate_migration_benefit(void* ptr, size_t size, int source_node, int target_node) const;
    bool should_migrate(void* ptr, size_t size, int source_node, int target_node) const;
    
public:
    MemoryMigration(size_t max_concurrent = 4, size_t batch_size = 1024 * 1024);
    ~MemoryMigration();
    
    bool migrate(void* ptr, size_t size, int target_node);
    bool schedule_migration(void* ptr, size_t size, int target_node, float priority = 1.0f);
    
    bool process_pending_migrations(size_t max_count = 10);
    void process_all_pending_migrations();
    
    size_t get_pending_migration_count() const;
    size_t get_completed_migration_count() const;
    size_t get_failed_migration_count() const;
    
    void enable_migration(bool enable) { migration_enabled_.store(enable, std::memory_order_relaxed); }
    bool is_migration_enabled() const { return migration_enabled_.load(std::memory_order_relaxed); }
    
    std::vector<MigrationTask> get_pending_migrations() const;
    std::vector<MigrationTask> get_recent_completed_migrations(size_t count = 10) const;
    
    void reset_stats();
};

class NUMAThreadBinder {
private:
    struct ThreadBinding {
        std::thread::id thread_id;
        int node;
        std::chrono::steady_clock::time_point bind_time;
        size_t allocations_on_node;
        size_t memory_on_node;
        bool is_bound;
    };
    
    std::unordered_map<std::thread::id, ThreadBinding> thread_bindings_;
    mutable std::mutex mutex_;
    AutomaticNUMADetection* numa_detection_;
    bool auto_binding_enabled_;
    
    int select_node_for_thread(std::thread::id tid) const;
    bool bind_thread_to_node_linux(std::thread::id tid, int node);
    bool bind_thread_to_node_portable(std::thread::id tid, int node);
    
public:
    explicit NUMAThreadBinder(AutomaticNUMADetection* detection = nullptr);
    ~NUMAThreadBinder();
    
    bool bind_thread(std::thread::id tid, int node = -1);
    bool bind_current_thread(int node = -1);
    bool unbind_thread(std::thread::id tid);
    bool unbind_current_thread();
    
    bool is_thread_bound(std::thread::id tid) const;
    int get_thread_node(std::thread::id tid) const;
    
    void enable_auto_binding(bool enable) { auto_binding_enabled_ = enable; }
    bool is_auto_binding_enabled() const { return auto_binding_enabled_; }
    
    void record_allocation(std::thread::id tid, int node, size_t size);
    void record_deallocation(std::thread::id tid, int node, size_t size);
    
    std::vector<ThreadBinding> get_thread_bindings() const;
    std::vector<std::pair<std::thread::id, int> > get_optimal_bindings() const;
    
    void auto_bind_all_threads();
    void reset_bindings();
};

class NUMAAwareAllocator {
private:
    struct AllocatorNode {
        int node_id;
        void* (*allocate)(size_t);
        void (*deallocate)(void*, size_t);
        size_t allocated;
        size_t freed;
        size_t peak;
    };
    
    std::vector<AllocatorNode> nodes_;
    AutomaticNUMADetection* numa_detection_;
    InterleavedAllocator* interleaved_allocator_;
    MemoryMigration* memory_migration_;
    NUMAThreadBinder* thread_binder_;
    mutable std::mutex mutex_;
    
    void* allocate_on_node_local(size_t size, int node);
    void deallocate_on_node_local(void* ptr, size_t size, int node);
    
public:
    NUMAAwareAllocator();
    ~NUMAAwareAllocator();
    
    bool initialize();
    
    void* allocate(size_t size, int node = -1);
    void* allocate_interleaved(size_t size, const std::vector<int>& nodes);
    void* allocate_optimal(size_t size, bool prefer_local = true);
    
    void deallocate(void* ptr, size_t size);
    
    bool migrate(void* ptr, size_t size, int target_node);
    
    size_t get_node_allocated(int node) const;
    size_t get_node_peak(int node) const;
    size_t get_total_allocated() const;
    size_t get_total_peak() const;
    
    void bind_current_thread_to_node(int node = -1);
    int get_current_thread_node() const;
    
    void enable_interleaving(bool enable);
    void enable_migration(bool enable);
    void enable_auto_binding(bool enable);
    
    void trim();
    void reset();
};

inline HwlocIntegration::HwlocIntegration()
    : initialized_(false)
    , node_count_(1)
{
    detect_topology();
}

inline HwlocIntegration::~HwlocIntegration() {
#if defined(__linux__) && !defined(__ANDROID__)
    if (initialized_) {
        hwloc_topology_destroy(topology_);
    }
#endif
}

inline void HwlocIntegration::detect_topology() {
#if defined(__linux__) && !defined(__ANDROID__)
    if (hwloc_topology_init(&topology_) != 0) {
        initialized_ = false;
        return;
    }
    
    if (hwloc_topology_load(topology_) != 0) {
        hwloc_topology_destroy(topology_);
        initialized_ = false;
        return;
    }
    
    int depth = hwloc_get_numa_obj_depth(topology_);
    if (depth <= 0) {
        node_count_ = 1;
        cpu_to_node_.push_back(0);
        node_to_cpus_.push_back(std::vector<int>(1, 0));
    } else {
        node_count_ = hwloc_get_nbobjs_by_depth(topology_, depth);
        cpu_to_node_.resize(hwloc_get_nbobjs_by_type(topology_, HWLOC_OBJ_PU));
        node_to_cpus_.resize(node_count_);
        
        for (int i = 0; i < node_count_; ++i) {
            hwloc_obj_t node = hwloc_get_numa_obj_by_depth(topology_, depth, i);
            hwloc_bitmap_foreach_begin(cpu, node->cpuset) {
                if (cpu >= 0 && cpu < static_cast<int>(cpu_to_node_.size())) {
                    cpu_to_node_[cpu] = i;
                    node_to_cpus_[i].push_back(cpu);
                }
            } hwloc_bitmap_foreach_end();
        }
    }
    
    initialized_ = true;
#else
    initialized_ = false;
    node_count_ = 1;
    cpu_to_node_.push_back(0);
    node_to_cpus_.push_back(std::vector<int>(1, 0));
#endif
}

inline int HwlocIntegration::get_node_for_cpu(int cpu) const {
    if (!initialized_ || cpu < 0 || cpu >= static_cast<int>(cpu_to_node_.size())) {
        return 0;
    }
    return cpu_to_node_[cpu];
}

inline std::vector<int> HwlocIntegration::get_cpus_for_node(int node) const {
    if (!initialized_ || node < 0 || node >= node_count_) {
        return std::vector<int>();
    }
    return node_to_cpus_[node];
}

inline void HwlocIntegration::bind_thread_to_node(std::thread::id tid, int node) {
    (void)tid;
    (void)node;
    
#if defined(__linux__) && !defined(__ANDROID__)
    if (!initialized_ || node < 0 || node >= node_count_) {
        return;
    }
    
    std::vector<int> cpus = get_cpus_for_node(node);
    if (!cpus.empty()) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int cpu : cpus) {
            CPU_SET(cpu, &cpuset);
        }
        
        pthread_t thread = pthread_self();
        pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    }
#endif
}

inline void HwlocIntegration::bind_current_thread_to_node(int node) {
    bind_thread_to_node(std::this_thread::get_id(), node);
}

inline AutomaticNUMADetection::AutomaticNUMADetection()
    : preferred_node_(0)
{
}

inline AutomaticNUMADetection::~AutomaticNUMADetection() = default;

inline bool AutomaticNUMADetection::initialize() {
    if (!hwloc_.is_initialized()) {
        return false;
    }
    
    node_count_ = hwloc_.get_node_count();
    node_metrics_.resize(node_count_);
    
    for (int i = 0; i < node_count_; ++i) {
        node_metrics_[i].node_id = i;
        node_metrics_[i].total_memory = hwloc_.get_node_memory(i);
        node_metrics_[i].free_memory = node_metrics_[i].total_memory;
        node_metrics_[i].allocated_memory = 0;
        node_metrics_[i].access_count = 0;
        node_metrics_[i].access_latency = 100.0f;
        node_metrics_[i].bandwidth = 51.2f;
        node_metrics_[i].is_preferred = (i == 0);
    }
    
    update_node_metrics();
    detect_pmem_nodes();
    detect_remote_nodes();
    
    return true;
}

inline int AutomaticNUMADetection::get_optimal_node_for_allocation(size_t size, bool prefer_local) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (node_metrics_.empty()) {
        return 0;
    }
    
    if (prefer_local) {
        int current_node = preferred_node_.load(std::memory_order_relaxed);
        if (current_node >= 0 && current_node < static_cast<int>(node_metrics_.size())) {
            if (node_metrics_[current_node].free_memory >= size) {
                return current_node;
            }
        }
    }
    
    int best_node = 0;
    size_t max_free = 0;
    
    for (const auto& metrics : node_metrics_) {
        if (metrics.free_memory > max_free) {
            max_free = metrics.free_memory;
            best_node = metrics.node_id;
        }
    }
    
    if (max_free >= size) {
        return best_node;
    }
    
    return 0;
}

inline void AutomaticNUMADetection::record_allocation(int node, size_t size) {
    if (node < 0 || node >= static_cast<int>(node_metrics_.size())) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    node_metrics_[node].allocated_memory += size;
    node_metrics_[node].free_memory -= size;
}

inline void AutomaticNUMADetection::record_deallocation(int node, size_t size) {
    if (node < 0 || node >= static_cast<int>(node_metrics_.size())) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    node_metrics_[node].allocated_memory -= size;
    node_metrics_[node].free_memory += size;
}

inline InterleavedAllocator::InterleavedAllocator(size_t default_granularity, bool use_huge_pages)
    : default_granularity_(default_granularity)
    , use_huge_pages_(use_huge_pages)
{
}

inline InterleavedAllocator::~InterleavedAllocator() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& region : regions_) {
        deallocate_interleaved_region(region.base);
    }
    regions_.clear();
}

inline void* InterleavedAllocator::allocate(size_t size, const std::vector<int>& nodes, size_t granularity) {
    if (size == 0 || nodes.empty()) {
        return nullptr;
    }
    
    if (granularity == 0) {
        granularity = default_granularity_;
    }
    
    void* ptr = allocate_interleaved_region(size, nodes, granularity);
    if (!ptr) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    InterleavedRegion region;
    region.base = ptr;
    region.size = size;
    region.nodes = nodes;
    region.granularity = granularity;
    region.allocated = 0;
    region.max_allocations = size / granularity;
    
    regions_.push_back(region);
    return ptr;
}

inline void InterleavedAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto it = regions_.begin(); it != regions_.end(); ++it) {
        if (it->base == ptr) {
            deallocate_interleaved_region(ptr);
            regions_.erase(it);
            break;
        }
    }
}

inline bool InterleavedAllocator::is_interleaved(void* ptr) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& region : regions_) {
        if (region.base == ptr) {
            return true;
        }
    }
    
    return false;
}

inline MemoryMigration::MemoryMigration(size_t max_concurrent, size_t batch_size)
    : migration_enabled_(true)
    , max_concurrent_migrations_(max_concurrent)
    , migration_batch_size_(batch_size)
{
}

inline MemoryMigration::~MemoryMigration() = default;

inline bool MemoryMigration::migrate(void* ptr, size_t size, int target_node) {
    if (!migration_enabled_.load(std::memory_order_relaxed) || !ptr || size == 0) {
        return false;
    }
    
#if defined(__linux__) && !defined(__ANDROID__)
    return migrate_pages_linux(ptr, size, target_node);
#else
    return migrate_pages_portable(ptr, size, target_node);
#endif
}

inline bool MemoryMigration::schedule_migration(void* ptr, size_t size, int target_node, float priority) {
    if (!ptr || size == 0) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    MigrationTask task;
    task.address = ptr;
    task.size = size;
    task.source_node = 0;
    task.target_node = target_node;
    task.scheduled_time = std::chrono::steady_clock::now();
    task.priority = priority;
    task.completed = false;
    task.failed = false;
    
    pending_migrations_.push_back(task);
    std::push_heap(pending_migrations_.begin(), pending_migrations_.end());
    
    return true;
}

inline bool MemoryMigration::process_pending_migrations(size_t max_count) {
    if (!migration_enabled_.load(std::memory_order_relaxed)) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t processed = 0;
    while (!pending_migrations_.empty() && processed < max_count) {
        std::pop_heap(pending_migrations_.begin(), pending_migrations_.end());
        MigrationTask task = pending_migrations_.back();
        pending_migrations_.pop_back();
        
        bool success = migrate(task.address, task.size, task.target_node);
        task.completed = true;
        task.failed = !success;
        
        completed_migrations_.push_back(task);
        processed++;
    }
    
    return processed > 0;
}

inline NUMAThreadBinder::NUMAThreadBinder(AutomaticNUMADetection* detection)
    : numa_detection_(detection)
    , auto_binding_enabled_(false)
{
}

inline NUMAThreadBinder::~NUMAThreadBinder() = default;

inline bool NUMAThreadBinder::bind_thread(std::thread::id tid, int node) {
    if (node < 0) {
        node = select_node_for_thread(tid);
    }
    
#if defined(__linux__) && !defined(__ANDROID__)
    bool success = bind_thread_to_node_linux(tid, node);
#else
    bool success = bind_thread_to_node_portable(tid, node);
#endif
    
    if (success) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        ThreadBinding binding;
        binding.thread_id = tid;
        binding.node = node;
        binding.bind_time = std::chrono::steady_clock::now();
        binding.allocations_on_node = 0;
        binding.memory_on_node = 0;
        binding.is_bound = true;
        
        thread_bindings_[tid] = binding;
    }
    
    return success;
}

inline bool NUMAThreadBinder::bind_current_thread(int node) {
    return bind_thread(std::this_thread::get_id(), node);
}

inline bool NUMAThreadBinder::is_thread_bound(std::thread::id tid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = thread_bindings_.find(tid);
    if (it != thread_bindings_.end()) {
        return it->second.is_bound;
    }
    
    return false;
}

inline void NUMAThreadBinder::record_allocation(std::thread::id tid, int node, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = thread_bindings_.find(tid);
    if (it != thread_bindings_.end()) {
        if (it->second.node == node) {
            it->second.allocations_on_node++;
            it->second.memory_on_node += size;
        }
    }
}

inline NUMAAwareAllocator::NUMAAwareAllocator()
    : numa_detection_(nullptr)
    , interleaved_allocator_(nullptr)
    , memory_migration_(nullptr)
    , thread_binder_(nullptr)
{
}

inline NUMAAwareAllocator::~NUMAAwareAllocator() {
    if (numa_detection_) delete numa_detection_;
    if (interleaved_allocator_) delete interleaved_allocator_;
    if (memory_migration_) delete memory_migration_;
    if (thread_binder_) delete thread_binder_;
}

inline bool NUMAAwareAllocator::initialize() {
    numa_detection_ = new AutomaticNUMADetection();
    if (!numa_detection_->initialize()) {
        return false;
    }
    
    interleaved_allocator_ = new InterleavedAllocator();
    memory_migration_ = new MemoryMigration();
    thread_binder_ = new NUMAThreadBinder(numa_detection_);
    
    int node_count = numa_detection_->get_preferred_node() + 1;
    nodes_.resize(node_count);
    
    for (int i = 0; i < node_count; ++i) {
        nodes_[i].node_id = i;
        nodes_[i].allocated = 0;
        nodes_[i].freed = 0;
        nodes_[i].peak = 0;
    }
    
    return true;
}

inline void* NUMAAwareAllocator::allocate(size_t size, int node) {
    if (node < 0) {
        node = numa_detection_->get_optimal_node_for_allocation(size, true);
    }
    
    if (node < 0 || node >= static_cast<int>(nodes_.size())) {
        return nullptr;
    }
    
    void* ptr = allocate_on_node_local(size, node);
    if (ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        nodes_[node].allocated += size;
        if (nodes_[node].allocated > nodes_[node].peak) {
            nodes_[node].peak = nodes_[node].allocated;
        }
        
        numa_detection_->record_allocation(node, size);
        
        if (thread_binder_) {
            thread_binder_->record_allocation(std::this_thread::get_id(), node, size);
        }
    }
    
    return ptr;
}

inline void NUMAAwareAllocator::deallocate(void* ptr, size_t size) {
    if (!ptr) return;
    
    int node = 0;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& node_info : nodes_) {
        if (node_info.allocated >= size) {
            node = node_info.node_id;
            node_info.allocated -= size;
            node_info.freed += size;
            break;
        }
    }
    
    deallocate_on_node_local(ptr, size, node);
    numa_detection_->record_deallocation(node, size);
}

inline void NUMAAwareAllocator::bind_current_thread_to_node(int node) {
    if (thread_binder_) {
        thread_binder_->bind_current_thread(node);
    }
}

inline size_t NUMAAwareAllocator::get_total_allocated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t total = 0;
    for (const auto& node : nodes_) {
        total += node.allocated;
    }
    return total;
}

} // namespace neuro_os::memory

#endif // NEURO_OS_MEMORY_NUMA_OPTIMIZATIONS_HPP