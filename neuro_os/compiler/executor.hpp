#ifndef NEURO_OS_COMPILER_EXECUTOR_HPP
#define NEURO_OS_COMPILER_EXECUTOR_HPP

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <thread>
#include <mutex>
#include <chrono>

#include "codegen.hpp"
#include "target.hpp"

namespace neuro_os::compiler {

#if __has_include(<sys/mman.h>)
#include <sys/mman.h>
#define NEURO_OS_HAS_MMAP 1
#else
#define NEURO_OS_HAS_MMAP 0
#endif

#if __has_include(<windows.h>)
#include <windows.h>
#define NEURO_OS_HAS_WINDOWS 1
#else
#define NEURO_OS_HAS_WINDOWS 0
#endif

enum ExecutionModel {
    Interpreted,
    JITCompiled,
    AOTCompiled
};

struct ExecutionResult {
    bool success;
    std::string error_message;
    uint64_t execution_time_us;
    int64_t return_value;
    
    ExecutionResult() : success(false), execution_time_us(0), return_value(0) {}
};

class MemoryManager {
public:
    MemoryManager();
    ~MemoryManager();
    
    void* allocate_code(size_t size);
    void* allocate_data(size_t size);
    
    void deallocate(void* ptr, size_t size);
    
    void set_executable(void* ptr, size_t size);
    void set_readonly(void* ptr, size_t size);

private:
    struct Allocation {
        void* ptr;
        size_t size;
    };
    std::vector<Allocation> allocations_;
    std::mutex mutex_;
};

inline MemoryManager::MemoryManager() {}

inline MemoryManager::~MemoryManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < allocations_.size(); ++i) {
#if NEURO_OS_HAS_MMAP
        if (allocations_[i].ptr) munmap(allocations_[i].ptr, allocations_[i].size);
#elif NEURO_OS_HAS_WINDOWS
        if (allocations_[i].ptr) VirtualFree(allocations_[i].ptr, 0, MEM_RELEASE);
#endif
    }
}

inline void* MemoryManager::allocate_code(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    void* ptr = NULL;
    size = (size + 4095) & ~4095;
    
#if NEURO_OS_HAS_MMAP
    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#elif NEURO_OS_HAS_WINDOWS
    ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
                       PAGE_EXECUTE_READWRITE);
#endif
    
    if (ptr && ptr != MAP_FAILED) {
        Allocation alloc = {ptr, size};
        allocations_.push_back(alloc);
        return ptr;
    }
    
    return NULL;
}

inline void* MemoryManager::allocate_data(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    void* ptr = NULL;
    size = (size + 4095) & ~4095;
    
#if NEURO_OS_HAS_MMAP
    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#elif NEURO_OS_HAS_WINDOWS
    ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
                       PAGE_READWRITE);
#endif
    
    if (ptr && ptr != MAP_FAILED) {
        Allocation alloc = {ptr, size};
        allocations_.push_back(alloc);
        return ptr;
    }
    
    return NULL;
}

inline void MemoryManager::deallocate(void* ptr, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
#if NEURO_OS_HAS_MMAP
    munmap(ptr, size);
#elif NEURO_OS_HAS_WINDOWS
    VirtualFree(ptr, 0, MEM_RELEASE);
#endif
    
    for (size_t i = 0; i < allocations_.size(); ++i) {
        if (allocations_[i].ptr == ptr) {
            allocations_.erase(allocations_.begin() + i);
            break;
        }
    }
}

inline void MemoryManager::set_executable(void* ptr, size_t size) {
#if NEURO_OS_HAS_MMAP
    mprotect(ptr, size, PROT_READ | PROT_EXEC);
#elif NEURO_OS_HAS_WINDOWS
    DWORD old_prot;
    VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &old_prot);
#endif
}

inline void MemoryManager::set_readonly(void* ptr, size_t size) {
#if NEURO_OS_HAS_MMAP
    mprotect(ptr, size, PROT_READ);
#elif NEURO_OS_HAS_WINDOWS
    DWORD old_prot;
    VirtualProtect(ptr, size, PAGE_READONLY, &old_prot);
#endif
}

class JITFunction {
public:
    template<typename Fn>
    Fn get_pointer() const {
        return reinterpret_cast<Fn>(pointer_);
    }
    
    void* get_pointer() const { return pointer_; }
    const std::string& get_name() const { return name_; }
    size_t get_code_size() const { return code_size_; }

private:
    friend class JITCompiler;
    
    std::string name_;
    void* pointer_;
    size_t code_size_;
};

class JITCompiler {
public:
    JITCompiler();
    explicit JITCompiler(const TargetMachine& target);
    ~JITCompiler();
    
    void set_optimization_level(int level);
    void set_execution_model(ExecutionModel model);
    
    template<typename Fn>
    Fn* compile(const Module& ir_module, const std::string& func_name) {
        JITFunction* result = compile_function(ir_module, func_name);
        if (result && result->get_pointer()) {
            return result->get_pointer<Fn*>();
        }
        return NULL;
    }
    
    JITFunction* compile_function(const Module& ir_module, 
                                                    const std::string& func_name);
    
    ExecutionResult execute(const std::string& func_name, 
                           const std::vector<void*>& args);
    
    template<typename R, typename T1>
    R execute(const std::string& func_name, T1 arg1) {
        typedef R(*FuncType)(T1);
        auto it = compiled_functions_.find(func_name);
        if (it == compiled_functions_.end()) {
            return R();
        }
        
        FuncType fn = reinterpret_cast<FuncType>(it->second->get_pointer());
        if (!fn) {
            return R();
        }
        
        return fn(arg1);
    }
    
    void* get_function_pointer(const std::string& func_name);
    
    void clear_cache();
    size_t get_cache_size() const { return compiled_functions_.size(); }

private:
    TargetMachine target_;
    CodeGenerator codegen_;
    MemoryManager memory_manager_;
    ExecutionModel execution_model_;
    int optimization_level_;
    
    std::unordered_map<std::string, std::shared_ptr<JITFunction> > compiled_functions_;
    
    std::shared_ptr<JITFunction> create_jit_function(
        const std::string& name, 
        const std::vector<uint8_t>& code);
    
    void optimize_and_emit(const Module& ir_module);
};

inline JITCompiler::JITCompiler() 
    : target_(TargetMachine::detect()),
      execution_model_(JITCompiled),
      optimization_level_(2) {
}

inline JITCompiler::JITCompiler(const TargetMachine& target) 
    : target_(target),
      execution_model_(JITCompiled),
      optimization_level_(2) {
}

inline JITCompiler::~JITCompiler() {}

inline void JITCompiler::set_optimization_level(int level) {
    optimization_level_ = level;
}

inline void JITCompiler::set_execution_model(ExecutionModel model) {
    execution_model_ = model;
}

inline JITFunction* JITCompiler::compile_function(
    const Module& ir_module, 
    const std::string& func_name) {
    
    codegen_.translate(ir_module);
    codegen_.optimize(optimization_level_);
    
    std::vector<uint8_t> machine_code = codegen_.compileToMachineCode();
    
    if (machine_code.empty()) {
        return NULL;
    }
    
    std::shared_ptr<JITFunction> jit_func = create_jit_function(func_name, machine_code);
    if (jit_func) {
        compiled_functions_[func_name] = jit_func;
    }
    
    return jit_func.get();
}

inline std::shared_ptr<JITFunction> JITCompiler::create_jit_function(
    const std::string& name, 
    const std::vector<uint8_t>& code) {
    
    void* ptr = memory_manager_.allocate_code(code.size());
    if (!ptr) {
        return std::shared_ptr<JITFunction>();
    }
    
    std::memcpy(ptr, code.data(), code.size());
    memory_manager_.set_executable(ptr, code.size());
    
    std::shared_ptr<JITFunction> jit_func(new JITFunction());
    jit_func->name_ = name;
    jit_func->pointer_ = ptr;
    jit_func->code_size_ = code.size();
    
    return jit_func;
}

inline ExecutionResult JITCompiler::execute(
    const std::string& func_name, 
    const std::vector<void*>& args) {
    
    ExecutionResult result;
    
    auto it = compiled_functions_.find(func_name);
    if (it == compiled_functions_.end()) {
        result.error_message = "Function not found: " + func_name;
        return result;
    }
    
    typedef int(*FuncType)(void**);
    FuncType fn = reinterpret_cast<FuncType>(it->second->get_pointer());
    if (!fn) {
        result.error_message = "Invalid function pointer";
        return result;
    }
    
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    
    void** args_array = const_cast<void**>(args.data());
    int ret = fn(args_array);
    
    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    result.execution_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end - start).count();
    
    result.return_value = ret;
    result.success = true;
    
    return result;
}

inline void* JITCompiler::get_function_pointer(const std::string& func_name) {
    auto it = compiled_functions_.find(func_name);
    if (it != compiled_functions_.end()) {
        return it->second->get_pointer();
    }
    return NULL;
}

inline void JITCompiler::clear_cache() {
    compiled_functions_.clear();
}

class ExecutionEngine {
public:
    ExecutionEngine();
    explicit ExecutionEngine(const TargetMachine& target);
    ~ExecutionEngine();
    
    void load_module(const Module& module);
    
    template<typename Fn>
    Fn* get_function(const std::string& name) {
        void* ptr = get_function_pointer(name);
        if (ptr) {
            return reinterpret_cast<Fn*>(ptr);
        }
        return NULL;
    }
    
    void* get_function_pointer(const std::string& name);
    
    template<typename R, typename T1>
    R call(const std::string& name, T1 arg1) {
        void* ptr = get_function_pointer(name);
        if (!ptr) {
            return R();
        }
        
        typedef R(*FuncPtr)(T1);
        FuncPtr fn = reinterpret_cast<FuncPtr>(ptr);
        return fn(arg1);
    }
    
    void add_global(const std::string& name, void* data);
    void* get_global(const std::string& name) const;

private:
    TargetMachine target_;
    std::shared_ptr<JITCompiler> jit_;
    std::unordered_map<std::string, void*> globals_;
};

inline ExecutionEngine::ExecutionEngine() : target_(TargetMachine::detect()) {
    jit_ = std::shared_ptr<JITCompiler>(new JITCompiler(target_));
}

inline ExecutionEngine::ExecutionEngine(const TargetMachine& target) 
    : target_(target) {
    jit_ = std::shared_ptr<JITCompiler>(new JITCompiler(target));
}

inline ExecutionEngine::~ExecutionEngine() {}

inline void ExecutionEngine::load_module(const Module& module) {
    std::vector<Function*> funcs = module.getFunctions();
    for (size_t i = 0; i < funcs.size(); ++i) {
        jit_->compile_function(module, funcs[i]->getName());
    }
}

inline void* ExecutionEngine::get_function_pointer(const std::string& name) {
    return jit_->get_function_pointer(name);
}

inline void ExecutionEngine::add_global(const std::string& name, void* data) {
    globals_[name] = data;
}

inline void* ExecutionEngine::get_global(const std::string& name) const {
    std::unordered_map<std::string, void*>::const_iterator it = globals_.find(name);
    if (it != globals_.end()) {
        return it->second;
    }
    return NULL;
}

class Runtime {
public:
    static Runtime& instance();
    
    JITCompiler& get_jit() { return *jit_; }
    ExecutionEngine& get_engine() { return *engine_; }
    TargetMachine& get_host_target() { return host_target_; }
    
    template<typename Fn>
    Fn* compile_and_run(const Module& module, const std::string& func_name) {
        Fn* func = jit_->compile<Fn*>(module, func_name);
        return func;
    }

private:
    Runtime();
    Runtime(const Runtime&);
    Runtime& operator=(const Runtime&);
    
    TargetMachine host_target_;
    std::shared_ptr<JITCompiler> jit_;
    std::shared_ptr<ExecutionEngine> engine_;
};

inline Runtime& Runtime::instance() {
    static Runtime instance;
    return instance;
}

inline Runtime::Runtime() : host_target_(TargetMachine::detect()) {
    jit_ = std::shared_ptr<JITCompiler>(new JITCompiler(host_target_));
    engine_ = std::shared_ptr<ExecutionEngine>(new ExecutionEngine(host_target_));
}

}

#endif
