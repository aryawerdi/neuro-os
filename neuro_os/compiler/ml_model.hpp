#pragma once

#include "optimization_decision.hpp"
#include <vector>
#include <memory>
#include <random>

namespace neuro_os::compiler {

struct IRFeatures {
    size_t instruction_count = 0;
    size_t loop_depth = 0;
    size_t call_count = 0;
    size_t branch_count = 0;
    size_t memory_ops = 0;
    size_t arithmetic_ops = 0;
    size_t function_count = 0;
    size_t basic_block_count = 0;
    size_t total_uses = 0;
    size_t constant_count = 0;
    size_t phi_nodes = 0;
    
    std::vector<float> to_vector() const;
};

inline std::vector<float> IRFeatures::to_vector() const {
    std::vector<float> result;
    result.reserve(11);
    result.push_back(static_cast<float>(instruction_count));
    result.push_back(static_cast<float>(loop_depth));
    result.push_back(static_cast<float>(call_count));
    result.push_back(static_cast<float>(branch_count));
    result.push_back(static_cast<float>(memory_ops));
    result.push_back(static_cast<float>(arithmetic_ops));
    result.push_back(static_cast<float>(function_count));
    result.push_back(static_cast<float>(basic_block_count));
    result.push_back(static_cast<float>(total_uses));
    result.push_back(static_cast<float>(constant_count));
    result.push_back(static_cast<float>(phi_nodes));
    return result;
}

class MLModel {
public:
    virtual ~MLModel() noexcept = default;
    
    virtual OptimizationDecision predict(const IRFeatures& features) = 0;
    virtual void train(const std::vector<IRFeatures>& features,
                      const std::vector<OptimizationDecision>& labels) = 0;
    virtual float evaluate() = 0;
    virtual void save(const std::string& path) = 0;
    virtual void load(const std::string& path) = 0;
};

class HeuristicModel : public MLModel {
public:
    HeuristicModel();
    
    OptimizationDecision predict(const IRFeatures& features) override;
    void train(const std::vector<IRFeatures>& features,
              const std::vector<OptimizationDecision>& labels) override;
    float evaluate() override;
    void save(const std::string& path) override;
    void load(const std::string& path) override;
    
private:
    float calculate_inlining_score(const IRFeatures& features);
    float calculate_vectorization_score(const IRFeatures& features);
    float calculate_dce_score(const IRFeatures& features);
    
    std::vector<IRFeatures> training_features_;
    std::vector<OptimizationDecision> training_labels_;
    std::mt19937 rng_;
};

class TorchModel : public MLModel {
public:
    TorchModel();
    ~TorchModel() noexcept override;
    
    OptimizationDecision predict(const IRFeatures& features) override;
    void train(const std::vector<IRFeatures>& features,
              const std::vector<OptimizationDecision>& labels) override;
    float evaluate() override;
    void save(const std::string& path) override;
    void load(const std::string& path) override;
    
    bool is_available() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

std::unique_ptr<MLModel> create_model(bool use_torch = false);

} // namespace neuro_os::compiler
