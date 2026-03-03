#ifndef NEURO_OS_COMPILER_IR_SSA_HPP
#define NEURO_OS_COMPILER_IR_SSA_HPP

#include "ir_builder.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

namespace neuro_os::compiler {

class PhiInst : public Instruction {
public:
    PhiInst(Type ty, const std::vector<Value*>& incoming_values, 
            const std::vector<BasicBlock*>& incoming_blocks);
    
    std::string getOpName() const override { return "phi"; }
    
    size_t getNumIncomingValues() const { return incoming_values_.size(); }
    Value* getIncomingValue(size_t idx) const { 
        return idx < incoming_values_.size() ? incoming_values_[idx] : nullptr; 
    }
    BasicBlock* getIncomingBlock(size_t idx) const { 
        return idx < incoming_blocks_.size() ? incoming_blocks_[idx] : nullptr; 
    }
    
    void addIncoming(Value* val, BasicBlock* block);
    void removeIncoming(size_t idx);
    
private:
    std::vector<Value*> incoming_values_;
    std::vector<BasicBlock*> incoming_blocks_;
};

class SSABuilder {
public:
    explicit SSABuilder(Module* mod = nullptr);
    
    void setModule(Module* mod) { module_ = mod; }
    
    void convertToSSA(Function* func);
    void renameVariables(Function* func);
    
    PhiInst* CreatePhi(Type ty, const std::string& name = "");
    
    void placePhiNodes(Function* func);
    void computeDominanceFrontier(Function* func);
    
    struct VariableInfo {
        std::vector<BasicBlock*> defs;
        std::unordered_set<BasicBlock*> uses;
        std::unordered_set<BasicBlock*> phi_required;
    };
    
    const std::unordered_map<std::string, VariableInfo>& getVariableInfo() const { 
        return var_info_; 
    }
    
private:
    Module* module_ = nullptr;
    std::unordered_map<std::string, VariableInfo> var_info_;
    std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*> > dominance_frontier_;
    std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*> > dominators_;
    
    void collectVariableInfo(Function* func);
    void computeDominators(Function* func);
    void computeDominanceFrontierForBlock(BasicBlock* bb);
    
    std::unordered_map<std::string, std::vector<Value*> > current_versions_;
    std::unordered_map<const BasicBlock*, std::unordered_map<std::string, Value*> > incoming_versions_;
    
    void renameBlock(BasicBlock* bb);
    void renameOperand(Value*& operand, const std::string& var_name);
};

PhiInst::PhiInst(Type ty, const std::vector<Value*>& incoming_values,
                 const std::vector<BasicBlock*>& incoming_blocks)
    : Instruction(OpCode::Phi, ty), 
      incoming_values_(incoming_values),
      incoming_blocks_(incoming_blocks) {
    for (auto* val : incoming_values_) {
        addOperand(val);
    }
}

void PhiInst::addIncoming(Value* val, BasicBlock* block) {
    incoming_values_.push_back(val);
    incoming_blocks_.push_back(block);
    addOperand(val);
}

void PhiInst::removeIncoming(size_t idx) {
    if (idx < incoming_values_.size()) {
        incoming_values_.erase(incoming_values_.begin() + idx);
        incoming_blocks_.erase(incoming_blocks_.begin() + idx);
        operands.erase(operands.begin() + idx);
    }
}

SSABuilder::SSABuilder(Module* mod) : module_(mod) {}

void SSABuilder::convertToSSA(Function* func) {
    collectVariableInfo(func);
    computeDominators(func);
    computeDominanceFrontier(func);
    placePhiNodes(func);
    renameVariables(func);
}

void SSABuilder::collectVariableInfo(Function* func) {
    var_info_.clear();
    
    for (auto* bb : func->getBasicBlocks()) {
        for (auto* inst : bb->getInstructions()) {
            if (!inst->getName().empty()) {
                auto& info = var_info_[inst->getName()];
                info.defs.push_back(bb);
            }
            
            for (size_t i = 0; i < inst->getNumOperands(); ++i) {
                if (auto* named_val = dynamic_cast<Instruction*>(inst->getOperand(i))) {
                    if (!named_val->getName().empty()) {
                        var_info_[named_val->getName()].uses.insert(bb);
                    }
                }
            }
        }
    }
}

void SSABuilder::computeDominators(Function* func) {
    const auto& blocks = func->getBasicBlocks();
    if (blocks.empty()) return;
    
    dominators_.clear();
    
    for (auto* bb : blocks) {
        std::unordered_set<BasicBlock*> all_blocks(blocks.begin(), blocks.end());
        dominators_[bb] = all_blocks;
    }
    
    dominators_[blocks.front()].clear();
    dominators_[blocks.front()].insert(blocks.front());
    
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (size_t i = 1; i < blocks.size(); ++i) {
            auto* bb = blocks[i];
            auto& current_doms = dominators_[bb];
            
            std::unordered_set<BasicBlock*> new_doms;
            new_doms.insert(bb);
            
            std::vector<BasicBlock*> preds;
            for (auto* pred : blocks) {
                for (auto* inst : pred->getInstructions()) {
                    if (auto* br = dynamic_cast<BranchInst*>(inst)) {
                        if (br->isConditional()) {
                            if (br->getTrueDest() == bb || br->getFalseDest() == bb) {
                                preds.push_back(pred);
                            }
                        } else if (br->getTrueDest() == bb) {
                            preds.push_back(pred);
                        }
                    }
                }
            }
            
            if (!preds.empty()) {
                new_doms.insert(dominators_[preds[0]].begin(), dominators_[preds[0]].end());
                
                for (size_t j = 1; j < preds.size(); ++j) {
                    std::unordered_set<BasicBlock*> intersect;
                    for (auto* dom : new_doms) {
                        if (dominators_[preds[j]].find(dom) != dominators_[preds[j]].end()) {
                            intersect.insert(dom);
                        }
                    }
                    new_doms = std::move(intersect);
                }
            }
            
            if (current_doms != new_doms) {
                current_doms = std::move(new_doms);
                changed = true;
            }
        }
    }
}

void SSABuilder::computeDominanceFrontier(Function* func) {
    dominance_frontier_.clear();
    
    for (auto* bb : func->getBasicBlocks()) {
        computeDominanceFrontierForBlock(bb);
    }
}

void SSABuilder::computeDominanceFrontierForBlock(BasicBlock* bb) {
    auto& frontier = dominance_frontier_[bb];
    frontier.clear();
    
    std::vector<BasicBlock*> preds;
    for (auto* pred : bb->getParent()->getBasicBlocks()) {
        for (auto* inst : pred->getInstructions()) {
            if (auto* br = dynamic_cast<BranchInst*>(inst)) {
                if (br->isConditional()) {
                    if (br->getTrueDest() == bb || br->getFalseDest() == bb) {
                        preds.push_back(pred);
                    }
                } else if (br->getTrueDest() == bb) {
                    preds.push_back(pred);
                }
            }
        }
    }
    
    if (preds.size() > 1) {
        for (auto* pred : preds) {
            auto* runner = pred;
            while (runner && dominators_[bb].find(runner) == dominators_[bb].end()) {
                dominance_frontier_[runner].insert(bb);
                runner = preds[0];
            }
        }
    }
}

void SSABuilder::placePhiNodes(Function* func) {
    for (auto& [var_name, info] : var_info_) {
        std::unordered_set<BasicBlock*> worklist;
        for (auto* bb : info.defs) {
            worklist.insert(bb);
        }
        std::unordered_set<BasicBlock*> visited;
        
        while (!worklist.empty()) {
            auto* bb = *worklist.begin();
            worklist.erase(worklist.begin());
            
            auto it = dominance_frontier_.find(bb);
            if (it != dominance_frontier_.end()) {
                for (auto* frontier_bb : it->second) {
                    if (info.phi_required.find(frontier_bb) == info.phi_required.end()) {
                        info.phi_required.insert(frontier_bb);
                        
                        if (visited.find(frontier_bb) == visited.end()) {
                            worklist.insert(frontier_bb);
                            visited.insert(frontier_bb);
                        }
                    }
                }
            }
        }
    }
    
    for (auto& entry : var_info_) {
        const auto& var_name = entry.first;
        auto& info = entry.second;
        for (auto* bb : info.phi_required) {
            auto* phi = new PhiInst(Type::Int32, std::vector<Value*>(), std::vector<BasicBlock*>());
            phi->setName(var_name);
            bb->addFront(phi);
        }
    }
}

void SSABuilder::renameVariables(Function* func) {
    current_versions_.clear();
    incoming_versions_.clear();
    
    renameBlock(func->getEntryBlock());
}

void SSABuilder::renameBlock(BasicBlock* bb) {
    auto saved_versions = current_versions_;
    
    for (auto* inst : bb->getInstructions()) {
        if (!inst->getName().empty()) {
            current_versions_[inst->getName()].push_back(inst);
        }
        
        if (auto* phi = dynamic_cast<PhiInst*>(inst)) {
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i) {
                auto* val = phi->getIncomingValue(i);
                if (val && !val->getName().empty()) {
                    auto& versions = current_versions_[val->getName()];
                    if (!versions.empty()) {
                        phi->removeIncoming(i);
                        phi->addIncoming(versions.back(), phi->getIncomingBlock(i));
                    }
                }
            }
        }
        
        for (size_t i = 0; i < inst->getNumOperands(); ++i) {
            auto* operand = inst->getOperand(i);
            if (operand && !operand->getName().empty()) {
                auto& versions = current_versions_[operand->getName()];
                if (!versions.empty()) {
                    inst->operands[i].value = versions.back();
                }
            }
        }
    }
    
    for (auto* succ : bb->getParent()->getBasicBlocks()) {
        bool is_successor = false;
        for (auto* inst : bb->getInstructions()) {
            if (auto* br = dynamic_cast<BranchInst*>(inst)) {
                if (br->isConditional()) {
                    if (br->getTrueDest() == succ || br->getFalseDest() == succ) {
                        is_successor = true;
                        break;
                    }
                } else if (br->getTrueDest() == succ) {
                    is_successor = true;
                    break;
                }
            }
        }
        
        if (is_successor) {
            incoming_versions_[succ] = std::unordered_map<std::string, Value*>();
            for (auto& entry : current_versions_) {
                const auto& var_name = entry.first;
                auto& versions = entry.second;
                if (!versions.empty()) {
                    incoming_versions_[succ][var_name] = versions.back();
                }
            }
        }
    }
    
    current_versions_ = saved_versions;
}

PhiInst* SSABuilder::CreatePhi(Type ty, const std::string& name) {
    auto* phi = new PhiInst(ty, std::vector<Value*>(), std::vector<BasicBlock*>());
    phi->setName(name);
    return phi;
}

}

#endif