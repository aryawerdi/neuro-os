#ifndef NEURO_OS_COMPILER_IR_OPTIMIZATION_HPP
#define NEURO_OS_COMPILER_IR_OPTIMIZATION_HPP

#include "ir_builder.hpp"
#include "ir_analysis.hpp"
#include "ir_ssa.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <functional>

namespace neuro_os::compiler {

class ConstantPropagation {
public:
    explicit ConstantPropagation(Function* func);
    
    bool run();
    bool propagateConstants();
    
    Value* getConstantValue(Value* val) const;
    bool isConstant(Value* val) const;
    
private:
    Function* func_;
    std::unordered_map<Value*, Value*> constant_values_;
    std::unordered_set<Value*> worklist_;
    
    void initializeWorklist();
    void processInstruction(Instruction* inst);
    Value* evaluateConstant(Instruction* inst);
    bool isConstantExpression(Instruction* inst) const;
};

ConstantPropagation::ConstantPropagation(Function* func) : func_(func) {}

bool ConstantPropagation::run() {
    return propagateConstants();
}

bool ConstantPropagation::propagateConstants() {
    initializeWorklist();
    
    bool changed = false;
    while (!worklist_.empty()) {
        auto it = worklist_.begin();
        Value* val = *it;
        worklist_.erase(it);
        
        if (auto* inst = dynamic_cast<Instruction*>(val)) {
            processInstruction(inst);
            
            for (auto* user : func_->getBasicBlocks()) {
                for (auto* user_inst : user->getInstructions()) {
                    for (size_t i = 0; i < user_inst->getNumOperands(); ++i) {
                        if (user_inst->getOperand(i) == val) {
                            if (auto* constant = getConstantValue(val)) {
                                user_inst->operands[i].value = constant;
                                changed = true;
                                worklist_.insert(user_inst);
                            }
                        }
                    }
                }
            }
        }
    }
    
    return changed;
}

void ConstantPropagation::initializeWorklist() {
    constant_values_.clear();
    worklist_.clear();
    
    for (auto* bb : func_->getBasicBlocks()) {
        for (auto* inst : bb->getInstructions()) {
            if (isConstantExpression(inst)) {
                if (auto* constant = evaluateConstant(inst)) {
                    constant_values_[inst] = constant;
                    worklist_.insert(inst);
                }
            }
        }
    }
}

void ConstantPropagation::processInstruction(Instruction* inst) {
    if (isConstantExpression(inst)) {
        if (auto* constant = evaluateConstant(inst)) {
            constant_values_[inst] = constant;
        }
    }
}

Value* ConstantPropagation::evaluateConstant(Instruction* inst) {
    if (auto* binop = dynamic_cast<BinOpInst*>(inst)) {
        auto* lhs = getConstantValue(binop->getLHS());
        auto* rhs = getConstantValue(binop->getRHS());
        
        if (lhs && rhs) {
            return nullptr;
        }
    }
    
    return nullptr;
}

bool ConstantPropagation::isConstantExpression(Instruction* inst) const {
    if (auto* binop = dynamic_cast<BinOpInst*>(inst)) {
        return isConstant(binop->getLHS()) && isConstant(binop->getRHS());
    }
    
    return false;
}

Value* ConstantPropagation::getConstantValue(Value* val) const {
    auto it = constant_values_.find(val);
    return it != constant_values_.end() ? it->second : nullptr;
}

bool ConstantPropagation::isConstant(Value* val) const {
    return getConstantValue(val) != nullptr;
}

class DeadCodeElimination {
public:
    explicit DeadCodeElimination(Function* func);
    
    bool run();
    bool eliminateDeadCode();
    
private:
    Function* func_;
    std::unordered_set<Instruction*> live_instructions_;
    
    void computeLiveInstructions();
    bool isLive(Instruction* inst) const;
    void markLive(Instruction* inst);
};

DeadCodeElimination::DeadCodeElimination(Function* func) : func_(func) {}

bool DeadCodeElimination::run() {
    return eliminateDeadCode();
}

bool DeadCodeElimination::eliminateDeadCode() {
    computeLiveInstructions();
    
    bool changed = false;
    for (auto* bb : func_->getBasicBlocks()) {
        auto& instructions = const_cast<std::vector<Instruction*>&>(bb->getInstructions());
        for (auto it = instructions.begin(); it != instructions.end(); ) {
            auto* inst = *it;
            if (!isLive(inst) && !inst->isTerminator()) {
                it = instructions.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    
    return changed;
}

void DeadCodeElimination::computeLiveInstructions() {
    live_instructions_.clear();
    
    for (auto* bb : func_->getBasicBlocks()) {
        for (auto* inst : bb->getInstructions()) {
            if (inst->isTerminator() || dynamic_cast<StoreInst*>(inst)) {
                markLive(inst);
            }
        }
    }
    
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (auto* bb : func_->getBasicBlocks()) {
            for (auto* inst : bb->getInstructions()) {
                if (isLive(inst)) {
                    for (size_t i = 0; i < inst->getNumOperands(); ++i) {
                        if (auto* op_inst = dynamic_cast<Instruction*>(inst->getOperand(i))) {
                            if (!isLive(op_inst)) {
                                markLive(op_inst);
                                changed = true;
                            }
                        }
                    }
                }
            }
        }
    }
}

bool DeadCodeElimination::isLive(Instruction* inst) const {
    return live_instructions_.find(inst) != live_instructions_.end();
}

void DeadCodeElimination::markLive(Instruction* inst) {
    live_instructions_.insert(inst);
}

class CommonSubexpressionElimination {
public:
    explicit CommonSubexpressionElimination(Function* func);
    
    bool run();
    bool eliminateCommonSubexpressions();
    
private:
    Function* func_;
    std::unordered_map<std::string, Instruction*> expression_map_;
    
    std::string getExpressionKey(Instruction* inst) const;
    bool isCSEable(Instruction* inst) const;
};

CommonSubexpressionElimination::CommonSubexpressionElimination(Function* func) : func_(func) {}

bool CommonSubexpressionElimination::run() {
    return eliminateCommonSubexpressions();
}

bool CommonSubexpressionElimination::eliminateCommonSubexpressions() {
    expression_map_.clear();
    bool changed = false;
    
    for (auto* bb : func_->getBasicBlocks()) {
        auto& instructions = const_cast<std::vector<Instruction*>&>(bb->getInstructions());
        for (auto it = instructions.begin(); it != instructions.end(); ) {
            auto* inst = *it;
            
            if (isCSEable(inst)) {
                std::string key = getExpressionKey(inst);
                auto existing = expression_map_.find(key);
                
                if (existing != expression_map_.end()) {
                    for (auto* user_bb : func_->getBasicBlocks()) {
                        for (auto* user_inst : user_bb->getInstructions()) {
                            for (size_t i = 0; i < user_inst->getNumOperands(); ++i) {
                                if (user_inst->getOperand(i) == inst) {
                                    user_inst->operands[i].value = existing->second;
                                    changed = true;
                                }
                            }
                        }
                    }
                    
                    it = instructions.erase(it);
                    continue;
                } else {
                    expression_map_[key] = inst;
                }
            }
            
            ++it;
        }
    }
    
    return changed;
}

std::string CommonSubexpressionElimination::getExpressionKey(Instruction* inst) const {
    std::string key;
    
    if (auto* binop = dynamic_cast<BinOpInst*>(inst)) {
        key = "binop:" + std::to_string(static_cast<int>(binop->getOpCode())) + ":";
        key += std::to_string(reinterpret_cast<uintptr_t>(binop->getLHS())) + ":";
        key += std::to_string(reinterpret_cast<uintptr_t>(binop->getRHS()));
    } else if (auto* load = dynamic_cast<LoadInst*>(inst)) {
        key = "load:" + std::to_string(reinterpret_cast<uintptr_t>(load->getPointer()));
    }
    
    return key;
}

bool CommonSubexpressionElimination::isCSEable(Instruction* inst) const {
    return dynamic_cast<BinOpInst*>(inst) != nullptr ||
           dynamic_cast<LoadInst*>(inst) != nullptr;
}

class InstructionCombining {
public:
    explicit InstructionCombining(Function* func);
    
    bool run();
    bool combineInstructions();
    
private:
    Function* func_;
    
    bool combineBinaryOperation(BinOpInst* inst);
    bool foldConstants(BinOpInst* inst);
    bool strengthReduction(BinOpInst* inst);
};

InstructionCombining::InstructionCombining(Function* func) : func_(func) {}

bool InstructionCombining::run() {
    return combineInstructions();
}

bool InstructionCombining::combineInstructions() {
    bool changed = false;
    
    for (auto* bb : func_->getBasicBlocks()) {
        for (auto* inst : bb->getInstructions()) {
            if (auto* binop = dynamic_cast<BinOpInst*>(inst)) {
                if (combineBinaryOperation(binop)) {
                    changed = true;
                }
            }
        }
    }
    
    return changed;
}

bool InstructionCombining::combineBinaryOperation(BinOpInst* inst) {
    bool changed = false;
    
    if (foldConstants(inst)) {
        changed = true;
    }
    
    if (strengthReduction(inst)) {
        changed = true;
    }
    
    return changed;
}

bool InstructionCombining::foldConstants(BinOpInst* inst) {
    return false;
}

bool InstructionCombining::strengthReduction(BinOpInst* inst) {
    return false;
}

class Optimizer {
public:
    explicit Optimizer(Module* mod);
    
    void run();
    void runFunction(Function* func);
    
    void addPass(const std::string& name, std::function<bool(Function*)> pass);
    
private:
    Module* module_;
    std::unordered_map<std::string, std::function<bool(Function*)> > passes_;
    
    void initializePasses();
};

Optimizer::Optimizer(Module* mod) : module_(mod) {
    initializePasses();
}

void Optimizer::run() {
    for (auto* func : module_->getFunctions()) {
        runFunction(func);
    }
}

void Optimizer::runFunction(Function* func) {
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (auto& entry : passes_) {
            const auto& name = entry.first;
            auto& pass = entry.second;
            if (pass(func)) {
                changed = true;
            }
        }
    }
}

void Optimizer::addPass(const std::string& name, std::function<bool(Function*)> pass) {
    passes_[name] = pass;
}

void Optimizer::initializePasses() {
    addPass("constant-propagation", [](Function* func) -> bool {
        ConstantPropagation cp(func);
        return cp.run();
    });
    
    addPass("dead-code-elimination", [](Function* func) -> bool {
        DeadCodeElimination dce(func);
        return dce.run();
    });
    
    addPass("common-subexpression-elimination", [](Function* func) -> bool {
        CommonSubexpressionElimination cse(func);
        return cse.run();
    });
    
    addPass("instruction-combining", [](Function* func) -> bool {
        InstructionCombining ic(func);
        return ic.run();
    });
}

}

#endif