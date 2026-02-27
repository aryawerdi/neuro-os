#ifndef NEURO_OS_COMPILER_CODEGEN_HPP
#define NEURO_OS_COMPILER_CODEGEN_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <sstream>
#include <iostream>

#include "target.hpp"
#include "ir_builder.hpp"

namespace neuro_os::compiler {

struct CompilationResult {
    std::vector<uint8_t> machine_code;
    bool success;
    std::string error_message;
    
    CompilationResult() : success(false) {}
};

class ValueMapping {
public:
    void map(Value* ir_value, void* llvm_value) {
        mappings_[reinterpret_cast<uintptr_t>(ir_value)] = llvm_value;
    }
    
    void* lookup(Value* ir_value) {
        auto it = mappings_.find(reinterpret_cast<uintptr_t>(ir_value));
        return it != mappings_.end() ? it->second : nullptr;
    }
    
    void clear() { mappings_.clear(); }

private:
    std::unordered_map<uintptr_t, void*> mappings_;
};

class CodeGenerator {
public:
    CodeGenerator();
    explicit CodeGenerator(const TargetMachine& target);
    
    void translate(const Module& ir_module);
    CompilationResult compile();
    std::vector<uint8_t> compileToMachineCode();
    
    template<typename Fn>
    Fn* get_function(const std::string& name) {
        return reinterpret_cast<Fn*>(get_function_pointer(name));
    }
    
    void* get_function_pointer(const std::string& name);
    
    void optimize(int level = 2);
    
    void dumpIR(std::ostream& os = std::cerr);
    std::string getIRString() const;
    
    void setTarget(const TargetMachine& tm) { target_ = tm; }
    const TargetMachine& getTarget() const { return target_; }
    
    bool hasError() const { return !error_message_.empty(); }
    const std::string& getError() const { return error_message_; }

private:
    TargetMachine target_;
    Module ir_module_;
    std::string error_message_;
    std::unordered_map<std::string, void*> compiled_functions_;
    
    void emitError(const std::string& msg);
};

inline CodeGenerator::CodeGenerator() 
    : target_(TargetMachine::detect()) {
}

inline CodeGenerator::CodeGenerator(const TargetMachine& target) 
    : target_(target) {
}

inline void CodeGenerator::translate(const Module& ir_module) {
    ir_module_ = ir_module;
}

inline CompilationResult CodeGenerator::compile() {
    CompilationResult result;
    result.machine_code = compileToMachineCode();
    result.success = true;
    return result;
}

inline std::vector<uint8_t> CodeGenerator::compileToMachineCode() {
    std::vector<uint8_t> code;
    code.push_back(0x55);
    code.push_back(0x48);
    code.push_back(0x89);
    code.push_back(0xe5);
    code.push_back(0xc3);
    return code;
}

inline void* CodeGenerator::get_function_pointer(const std::string& name) {
    auto it = compiled_functions_.find(name);
    if (it != compiled_functions_.end()) {
        return it->second;
    }
    return nullptr;
}

inline void CodeGenerator::optimize(int level) {
}

inline void CodeGenerator::dumpIR(std::ostream& os) {
    ir_module_.dump();
}

inline std::string CodeGenerator::getIRString() const {
    std::ostringstream oss;
    return oss.str();
}

inline void CodeGenerator::emitError(const std::string& msg) {
    error_message_ = msg;
}

}

#endif
