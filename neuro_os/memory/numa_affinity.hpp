#ifndef NEURO_OS_MEMORY_NUMA_AFFINITY_HPP
#define NEURO_OS_MEMORY_NUMA_AFFINITY_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>
#include <chrono>
#include <algorithm>

#if defined(__linux__) && !defined(__ANDROID__)
#define NEURO_OS_HAVE_HWLOC 1
#endif

#if NEURO_OS_HAVE_HWLOC
#include <hwloc.h>
#endif

namespace neuro_os::memory {

enum class MemoryTier {
    DRAM,
    PMEM,
    DISK,
    REMOTE
};

struct NUMANodeInfo {
    int node_id;
    size_t total_memory;
    size_t free_memory;
    float bandwidth_gbps;
    float latency_ns;
    bool is_local;
};

inline NUMANodeInfo make_default_node_info() {
    NUMANodeInfo info;
    info.node_id = 0;
    info.total_memory = 0;
    info.free_memory = 0;
    info.bandwidth_gbps = 51.2f;
    info.latency_ns = 100.0f;
    info.is_local = true;
    return info;
}

class NUMAAffinity {
public:
    struct Topology {
        std::vector<NUMANodeInfo> nodes;
        int current_node;
        int node_count;
        bool hwloc_available;
    };

private:
    Topology topology_;
    bool initialized_;

    void detect_with_hwloc();
    void detect_portable();

public:
    NUMAAffinity();
    ~NUMAAffinity();

    static NUMAAffinity detect();

    bool is_initialized() const { return initialized_; }
    int get_current_node() const { return topology_.current_node; }
    int get_node_count() const { return topology_.node_count; }
    const Topology& get_topology() const { return topology_; }
    const NUMANodeInfo& get_node_info(int node_id) const;

    void* allocate_local(size_t size);
    void* allocate_on_node(size_t size, int node_id);
    void* allocate_optimal(size_t size, bool prefer_local = true);

    bool migrate(void* ptr, size_t size, int target_node);
    int get_ptr_node(const void* ptr) const;

    void bind_to_node(int node_id);
    void bind_current_thread();
    void unbind();

    size_t get_node_memory(int node_id) const;
    size_t get_node_free_memory(int node_id) const;

    bool is_local_access(const void* ptr) const;
    float estimate_access_latency(const void* ptr) const;
};

class MemoryPolicy {
public:
    enum class Strategy {
        LOCAL_FIRST,
        ROUND_ROBIN,
        INTERLEAVE,
        PREFER_LARGE,
        PREFER_FAST
    };

private:
    Strategy strategy_;
    int preferred_node_;
    bool interleaving_enabled_;
    size_t interleaving_granularity_;
    int round_robin_counter_;

public:
    MemoryPolicy();

    void set_strategy(Strategy s) { strategy_ = s; }
    void set_preferred_node(int node) { preferred_node_ = node; }
    void enable_interleaving(size_t granularity = 4096);
    void disable_interleaving() { interleaving_enabled_ = false; }

    Strategy get_strategy() const { return strategy_; }
    int get_preferred_node() const { return preferred_node_; }
    bool is_interleaving_enabled() const { return interleaving_enabled_; }
    size_t get_interleaving_granularity() const { return interleaving_granularity_; }

    int select_node(size_t size, const NUMAAffinity& affinity) const;
};

class PageMigration {
public:
    struct MigrationHint {
        void* address;
        size_t size;
        int source_node;
        int target_node;
        float estimated_benefit;
    };

private:
    int page_size_;

public:
    PageMigration();

    bool can_migrate() const;
    bool migrate_pages(void* ptr, size_t size, int target_node);
    std::vector<MigrationHint> analyze_migration_benefits(
        const void* ptr,
        size_t size,
        int current_node,
        const std::vector<int>& candidate_nodes
    ) const;

    int get_page_size() const { return page_size_; }
};

class NumaMempolicy {
public:
    enum class Policy {
        DEFAULT,
        BIND,
        INTERLEAVE,
        PREFERRED,
        LOCAL
    };

private:
    Policy policy_;
    std::vector<int> nodes_;

public:
    NumaMempolicy();

    void set_policy(Policy p, const std::vector<int>& nodes);
    Policy get_policy() const { return policy_; }
    const std::vector<int>& get_nodes() const { return nodes_; }

    static NumaMempolicy current();
    bool apply() const;
};

inline NUMAAffinity::NUMAAffinity()
    : initialized_(false)
    , topology_()
{
    detect_portable();
    initialized_ = true;
}

inline NUMAAffinity::~NUMAAffinity() = default;

inline NUMAAffinity NUMAAffinity::detect() {
    return NUMAAffinity();
}

inline void NUMAAffinity::detect_portable() {
    topology_.current_node = 0;
    topology_.node_count = 1;
    topology_.hwloc_available = false;
    topology_.nodes.clear();
    topology_.nodes.push_back(make_default_node_info());
}

#if NEURO_OS_HAVE_HWLOC
inline void NUMAAffinity::detect_with_hwloc() {
    hwloc_topology_t topology;
    hwloc_topology_init(&topology);
    hwloc_topology_load(topology);

    int depth = hwloc_get_numa_obj_depth(topology);
    if (depth <= 0) {
        detect_portable();
        hwloc_topology_destroy(topology);
        return;
    }

    topology_.node_count = hwloc_get_nbobjs_by_depth(topology, depth);
    topology_.nodes.clear();
    topology_.nodes.reserve(topology_.node_count);

    for (int i = 0; i < topology_.node_count; ++i) {
        hwloc_obj_t node = hwloc_get_numa_obj_by_depth(topology, depth, i);
        NUMANodeInfo info;
        info.node_id = i;
        info.total_memory = node->attr->numa.mem.total_kb * 1024;
        info.free_memory = node->attr->numa.mem.free_kb * 1024;
        info.bandwidth_gbps = 51.2f;
        info.latency_ns = 100.0f;
        info.is_local = (i == topology_.current_node);
        topology_.nodes.push_back(info);
    }

    topology_.hwloc_available = true;
    hwloc_topology_destroy(topology);
}
#endif

inline const NUMANodeInfo& NUMAAffinity::get_node_info(int node_id) const {
    if (node_id >= 0 && node_id < static_cast<int>(topology_.nodes.size())) {
        return topology_.nodes[static_cast<size_t>(node_id)];
    }
    static const NUMANodeInfo default_info = make_default_node_info();
    return default_info;
}

inline void* NUMAAffinity::allocate_local(size_t size) {
    return allocate_on_node(size, topology_.current_node);
}

inline void* NUMAAffinity::allocate_on_node(size_t size, int node_id) {
    (void)node_id;
    return std::malloc(size);
}

inline void* NUMAAffinity::allocate_optimal(size_t size, bool prefer_local) {
    if (prefer_local && topology_.node_count > 0) {
        return allocate_local(size);
    }
    return std::malloc(size);
}

inline bool NUMAAffinity::migrate(void* ptr, size_t size, int target_node) {
    (void)ptr;
    (void)size;
    (void)target_node;
    return false;
}

inline int NUMAAffinity::get_ptr_node(const void* ptr) const {
    (void)ptr;
    return 0;
}

inline void NUMAAffinity::bind_to_node(int node_id) {
    (void)node_id;
}

inline void NUMAAffinity::bind_current_thread() {
#if defined(__linux__)
    hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
    hwloc_get_cpubind(HWLOC_TOPOLOGY_FLAG_INCLUDE_DISCONNECTED, cpuset, HWLOC_CPUBIND_THREAD);
    hwloc_bitmap_free(cpuset);
#endif
}

inline void NUMAAffinity::unbind() {
}

inline size_t NUMAAffinity::get_node_memory(int node_id) const {
    const auto& info = get_node_info(node_id);
    return info.total_memory;
}

inline size_t NUMAAffinity::get_node_free_memory(int node_id) const {
    const auto& info = get_node_info(node_id);
    return info.free_memory;
}

inline bool NUMAAffinity::is_local_access(const void* ptr) const {
    (void)ptr;
    return true;
}

inline float NUMAAffinity::estimate_access_latency(const void* ptr) const {
    (void)ptr;
    return 100.0f;
}

inline MemoryPolicy::MemoryPolicy()
    : strategy_(Strategy::LOCAL_FIRST)
    , preferred_node_(0)
    , interleaving_enabled_(false)
    , interleaving_granularity_(4096)
    , round_robin_counter_(0)
{
}

inline void MemoryPolicy::enable_interleaving(size_t granularity) {
    interleaving_enabled_ = true;
    interleaving_granularity_ = granularity;
}

inline int MemoryPolicy::select_node(size_t size, const NUMAAffinity& affinity) const {
    auto strategy = strategy_;
    
    if (strategy == Strategy::LOCAL_FIRST) {
        return affinity.get_current_node();
    }
    else if (strategy == Strategy::ROUND_ROBIN) {
        return (preferred_node_ + round_robin_counter_) % affinity.get_node_count();
    }
    else if (strategy == Strategy::INTERLEAVE) {
        if (interleaving_enabled_) {
            return (reinterpret_cast<uintptr_t>(&strategy) / interleaving_granularity_) % static_cast<size_t>(affinity.get_node_count());
        }
        return affinity.get_current_node();
    }
    else if (strategy == Strategy::PREFER_LARGE) {
        int best_node = 0;
        size_t max_free = 0;
        for (int i = 0; i < affinity.get_node_count(); ++i) {
            size_t free = affinity.get_node_free_memory(i);
            if (free > max_free) {
                max_free = free;
                best_node = i;
            }
        }
        return best_node;
    }
    else if (strategy == Strategy::PREFER_FAST) {
        return affinity.get_current_node();
    }
    return affinity.get_current_node();
}

inline PageMigration::PageMigration()
    : page_size_(4096)
{
}

inline bool PageMigration::can_migrate() const {
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

inline bool PageMigration::migrate_pages(void* ptr, size_t size, int target_node) {
    (void)ptr;
    (void)size;
    (void)target_node;
    return false;
}

inline std::vector<PageMigration::MigrationHint> PageMigration::analyze_migration_benefits(
    const void* ptr,
    size_t size,
    int current_node,
    const std::vector<int>& candidate_nodes
) const {
    (void)ptr;
    (void)size;
    (void)current_node;
    (void)candidate_nodes;
    return std::vector<MigrationHint>();
}

inline NumaMempolicy::NumaMempolicy()
    : policy_(Policy::DEFAULT)
{
}

inline void NumaMempolicy::set_policy(Policy p, const std::vector<int>& nodes) {
    policy_ = p;
    nodes_ = nodes;
}

inline NumaMempolicy NumaMempolicy::current() {
    return NumaMempolicy();
}

inline bool NumaMempolicy::apply() const {
    return false;
}

}

#endif
