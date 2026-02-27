#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace neuro_os::compiler {

enum class InlineDecision {
    NoInline,
    Inline,
    InlineWithClone
};

enum class VectorizationDecision {
    NoVectorize,
    Vectorize,
    AutoVectorize
};

enum class RegisterAllocStrategy {
    Fast,
    Optimal,
    Aggressive
};

struct InlineConfig {
    InlineDecision decision = InlineDecision::NoInline;
    size_t depth = 0;
    size_t max_depth = 3;
    bool force_inline = false;
    size_t threshold = 10;
};

struct VectorizationConfig {
    VectorizationDecision decision = VectorizationDecision::NoVectorize;
    size_t vector_width = 4;
    bool allow_mixed_types = false;
};

struct RegisterAllocConfig {
    RegisterAllocStrategy strategy = RegisterAllocStrategy::Fast;
    size_t max_registers = 16;
    bool enable_peephole = true;
};

struct OptimizationDecision {
    InlineConfig inline_config;
    VectorizationConfig vectorization_config;
    RegisterAllocConfig register_config;
    
    bool enable_inlining = true;
    bool enable_constant_folding = true;
    bool enable_dead_code_elimination = true;
    bool enable_loop_unrolling = false;
    bool enable_loop_vectorization = false;
    
    float confidence = 0.0f;
    
    std::string to_string() const;
};

inline std::string OptimizationDecision::to_string() const {
    std::string result = "OptimizationDecision { ";
    result += "inlining: " + std::string(enable_inlining ? "yes" : "no");
    result += ", constant_folding: " + std::string(enable_constant_folding ? "yes" : "no");
    result += ", dce: " + std::string(enable_dead_code_elimination ? "yes" : "no");
    result += ", unrolling: " + std::string(enable_loop_unrolling ? "yes" : "no");
    result += ", vectorization: " + std::string(enable_loop_vectorization ? "yes" : "no");
    result += " }";
    return result;
}

} // namespace neuro_os::compiler
