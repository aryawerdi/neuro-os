#pragma once

#include "optimization_decision.hpp"
#include "ml_model.hpp"
#include <memory>
#include <vector>
#include <string>

namespace neuro_os::compiler {

struct OptimizationConfig {
    bool enable_inlining = true;
    bool enable_vectorization = false;
    bool enable_constant_folding = true;
    bool enable_dead_code_elimination = true;
    bool enable_loop_unrolling = false;
    bool enable_loop_vectorization = false;
    size_t max_inline_depth = 3;
    size_t inline_threshold = 10;
    size_t vectorization_width = 4;
};

class Module;
class Function;
class BasicBlock;
class Instruction;

class MLOptimizer {
public:
    MLOptimizer();
    explicit MLOptimizer(std::unique_ptr<MLModel> model);
    ~MLOptimizer();
    
    OptimizationConfig predict(const IRFeatures& features);
    void train(const std::vector<IRFeatures>& features,
               const std::vector<OptimizationConfig>& labels);
    
    void optimize(Module* module);
    void optimize_function(Function* func);
    
    void inline_functions(Module* module);
    void inline_function(Function* func, const std::string& target);
    
    void constant_fold(Module* module);
    void constant_fold_function(Function* func);
    
    void eliminate_dead_code(Module* module);
    void eliminate_dead_code_function(Function* func);
    
    void set_config(const OptimizationConfig& config);
    const OptimizationConfig& get_config() const;
    
    void set_model(std::unique_ptr<MLModel> model);
    
private:
    IRFeatures extract_features(Module* module);
    IRFeatures extract_features(Function* func);
    IRFeatures extract_features(BasicBlock* block);
    
    bool should_inline(const IRFeatures& features);
    bool should_vectorize(const IRFeatures& features);
    bool should_constant_fold(const IRFeatures& features);
    bool should_eliminate_dead_code(const IRFeatures& features);
    
    OptimizationConfig config_;
    std::unique_ptr<MLModel> model_;
};

class OptimizationPass {
public:
    virtual ~OptimizationPass() = default;
    virtual void run(Module* module) = 0;
    virtual const char* name() const = 0;
};

class InlinePass : public OptimizationPass {
public:
    InlinePass(size_t max_depth = 3, size_t threshold = 10);
    void run(Module* module) override;
    const char* name() const override;
    
private:
    size_t max_depth_;
    size_t threshold_;
};

class ConstantFoldPass : public OptimizationPass {
public:
    void run(Module* module) override;
    const char* name() const override;
};

class DeadCodeEliminationPass : public OptimizationPass {
public:
    void run(Module* module) override;
    const char* name() const override;
};

} // namespace neuro_os::compiler
