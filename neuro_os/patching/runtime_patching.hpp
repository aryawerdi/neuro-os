#ifndef NEURO_OS_PATCHING_RUNTIME_PATCHING_HPP
#define NEURO_OS_PATCHING_RUNTIME_PATCHING_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <map>

#if defined(__APPLE__)
    #include <mach/mach.h>
    #include <libkern/OSCacheControl.h>
#elif defined(__linux__)
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace neuro_os::patching {

class FunctionHook {
private:
    void* original_function_;
    void* hook_function_;
    std::vector<uint8_t> original_code_;
    std::vector<uint8_t> trampoline_code_;
    bool hooked_;
    
#if defined(__APPLE__)
    mach_port_t task_;
#elif defined(__linux__)
    int prot_flags_;
#endif

    bool make_memory_writable(void* address, size_t size);
    bool make_memory_executable(void* address, size_t size);
    bool flush_instruction_cache(void* address, size_t size);
    
    std::vector<uint8_t> create_trampoline(void* target, void* hook);
    std::vector<uint8_t> create_detour(void* from, void* to);

public:
    FunctionHook();
    ~FunctionHook();
    
    bool install(void* original_function, void* hook_function);
    bool remove();
    
    void* get_original() const;
    void* get_hook() const;
    bool is_hooked() const;
    
    template<typename T>
    T call_original(T args...) {
        if (!hooked_) {
            return T();
        }
        
        using FuncType = T(*)(...);
        FuncType original = reinterpret_cast<FuncType>(original_function_);
        return original(args);
    }
};

class HotPatcher {
private:
    std::map<void*, std::vector<uint8_t>> original_code_cache_;
    std::map<void*, FunctionHook> installed_hooks_;
    
    bool validate_patch_location(void* address, size_t size);
    bool create_safe_execution_environment();
    
public:
    HotPatcher();
    ~HotPatcher();
    
    bool apply_patch(void* address, const std::vector<uint8_t>& new_code);
    bool revert_patch(void* address);
    
    bool hook_function(void* original, void* hook);
    bool unhook_function(void* original);
    
    bool test_patch_in_sandbox(void* address, const std::vector<uint8_t>& code, 
                              std::function<bool()> test_function);
    
    std::vector<void*> get_patched_addresses() const;
    std::vector<void*> get_hooked_functions() const;
    
    bool has_patch(void* address) const;
    bool has_hook(void* function) const;
};

class TrampolineGenerator {
private:
    struct TrampolineInfo {
        void* target;
        void* hook;
        std::vector<uint8_t> code;
        size_t size;
    };
    
    std::vector<TrampolineInfo> trampolines_;
    void* trampoline_pool_;
    size_t pool_size_;
    size_t used_pool_;
    
#if defined(__APPLE__)
    vm_address_t allocated_memory_;
#elif defined(__linux__)
    void* allocated_memory_;
#endif

    void* allocate_executable_memory(size_t size);
    bool free_executable_memory(void* address, size_t size);
    
    std::vector<uint8_t> generate_x64_trampoline(void* target, void* hook);
    std::vector<uint8_t> generate_arm64_trampoline(void* target, void* hook);
    
public:
    TrampolineGenerator(size_t pool_size = 4096);
    ~TrampolineGenerator();
    
    void* create_trampoline(void* target, void* hook);
    bool destroy_trampoline(void* trampoline);
    
    size_t get_available_pool() const;
    size_t get_used_pool() const;
    
    bool resize_pool(size_t new_size);
};

class PatchTester {
private:
    struct TestEnvironment {
        std::vector<uint8_t> original_code;
        std::vector<uint8_t> test_code;
        void* test_address;
        bool test_passed;
    };
    
    std::vector<TestEnvironment> test_environments_;
    HotPatcher hot_patcher_;
    
    void* allocate_test_memory(size_t size);
    bool free_test_memory(void* address, size_t size);
    
    bool setup_isolated_execution();
    bool cleanup_isolated_execution();
    
public:
    PatchTester();
    ~PatchTester();
    
    bool test_patch(void* address, const std::vector<uint8_t>& patch_code,
                   std::function<bool()> test_function);
    
    bool test_hook(void* original, void* hook,
                  std::function<bool()> pre_test,
                  std::function<bool()> post_test);
    
    bool run_comprehensive_test(void* address, const std::vector<uint8_t>& patch_code,
                               const std::vector<std::function<bool()>>& test_cases);
    
    std::vector<bool> get_test_results() const;
    void clear_test_results();
};

// Inline implementations
inline FunctionHook::FunctionHook() 
    : original_function_(nullptr), hook_function_(nullptr), hooked_(false) {
#if defined(__APPLE__)
    task_ = mach_task_self();
#endif
}

inline FunctionHook::~FunctionHook() {
    if (hooked_) {
        remove();
    }
}

inline void* FunctionHook::get_original() const {
    return original_function_;
}

inline void* FunctionHook::get_hook() const {
    return hook_function_;
}

inline bool FunctionHook::is_hooked() const {
    return hooked_;
}

inline HotPatcher::HotPatcher() = default;

inline HotPatcher::~HotPatcher() {
    for (auto& [address, _] : installed_hooks_) {
        unhook_function(address);
    }
    
    for (auto& [address, _] : original_code_cache_) {
        revert_patch(address);
    }
}

inline std::vector<void*> HotPatcher::get_patched_addresses() const {
    std::vector<void*> addresses;
    for (const auto& [address, _] : original_code_cache_) {
        addresses.push_back(address);
    }
    return addresses;
}

inline std::vector<void*> HotPatcher::get_hooked_functions() const {
    std::vector<void*> functions;
    for (const auto& [func, _] : installed_hooks_) {
        functions.push_back(func);
    }
    return functions;
}

inline bool HotPatcher::has_patch(void* address) const {
    return original_code_cache_.find(address) != original_code_cache_.end();
}

inline bool HotPatcher::has_hook(void* function) const {
    return installed_hooks_.find(function) != installed_hooks_.end();
}

inline TrampolineGenerator::TrampolineGenerator(size_t pool_size) 
    : pool_size_(pool_size), used_pool_(0), trampoline_pool_(nullptr),
      allocated_memory_(0) {
    trampoline_pool_ = allocate_executable_memory(pool_size_);
}

inline TrampolineGenerator::~TrampolineGenerator() {
    for (const auto& trampoline : trampolines_) {
        destroy_trampoline(trampoline.code.data());
    }
    
    if (trampoline_pool_) {
        free_executable_memory(trampoline_pool_, pool_size_);
    }
}

inline size_t TrampolineGenerator::get_available_pool() const {
    return pool_size_ - used_pool_;
}

inline size_t TrampolineGenerator::get_used_pool() const {
    return used_pool_;
}

inline PatchTester::PatchTester() = default;

inline PatchTester::~PatchTester() {
    clear_test_results();
}

inline std::vector<bool> PatchTester::get_test_results() const {
    std::vector<bool> results;
    for (const auto& env : test_environments_) {
        results.push_back(env.test_passed);
    }
    return results;
}

inline void PatchTester::clear_test_results() {
    for (auto& env : test_environments_) {
        if (env.test_address) {
            free_test_memory(env.test_address, env.test_code.size());
        }
    }
    test_environments_.clear();
}

} // namespace neuro_os::patching

#endif // NEURO_OS_PATCHING_RUNTIME_PATCHING_HPP