#ifndef NEURO_OS_COMPILER_IR_VISITOR_HPP
#define NEURO_OS_COMPILER_IR_VISITOR_HPP

#include "ir_builder.hpp"
#include <vector>
#include <string>
#include <memory>

namespace neuro_os::compiler {

std::vector<const BasicBlock*> getPredecessors(const BasicBlock* bb);

class IRVisitor {
public:
    virtual ~IRVisitor() {}
    
    virtual void visitModule(const Module* mod);
    virtual void visitFunction(const Function* func);
    virtual void visitBasicBlock(const BasicBlock* bb);
    virtual void visitInstruction(const Instruction* inst);
    
    virtual void visitAllocaInst(const AllocaInst* inst);
    virtual void visitLoadInst(const LoadInst* inst);
    virtual void visitStoreInst(const StoreInst* inst);
    virtual void visitBinOpInst(const BinOpInst* inst);
    virtual void visitICmpInst(const ICmpInst* inst);
    virtual void visitCallInst(const CallInst* inst);
    virtual void visitBranchInst(const BranchInst* inst);
    virtual void visitReturnInst(const ReturnInst* inst);
    virtual void visitSextInst(const SextInst* inst);
    virtual void visitZextInst(const ZextInst* inst);
};

void IRVisitor::visitModule(const Module* mod) {
    for (const auto* func : mod->getFunctions()) {
        visitFunction(func);
    }
}

void IRVisitor::visitFunction(const Function* func) {
    for (const auto* bb : func->getBasicBlocks()) {
        visitBasicBlock(bb);
    }
}

void IRVisitor::visitBasicBlock(const BasicBlock* bb) {
    for (const auto* inst : bb->getInstructions()) {
        visitInstruction(inst);
    }
}

void IRVisitor::visitInstruction(const Instruction* inst) {
    if (auto* alloca = dynamic_cast<const AllocaInst*>(inst)) {
        visitAllocaInst(alloca);
    } else if (auto* load = dynamic_cast<const LoadInst*>(inst)) {
        visitLoadInst(load);
    } else if (auto* store = dynamic_cast<const StoreInst*>(inst)) {
        visitStoreInst(store);
    } else if (auto* binop = dynamic_cast<const BinOpInst*>(inst)) {
        visitBinOpInst(binop);
    } else if (auto* icmp = dynamic_cast<const ICmpInst*>(inst)) {
        visitICmpInst(icmp);
    } else if (auto* call = dynamic_cast<const CallInst*>(inst)) {
        visitCallInst(call);
    } else if (auto* br = dynamic_cast<const BranchInst*>(inst)) {
        visitBranchInst(br);
    } else if (auto* ret = dynamic_cast<const ReturnInst*>(inst)) {
        visitReturnInst(ret);
    } else if (auto* sext = dynamic_cast<const SextInst*>(inst)) {
        visitSextInst(sext);
    } else if (auto* zext = dynamic_cast<const ZextInst*>(inst)) {
        visitZextInst(zext);
    }
}

void IRVisitor::visitAllocaInst(const AllocaInst*) {}
void IRVisitor::visitLoadInst(const LoadInst*) {}
void IRVisitor::visitStoreInst(const StoreInst*) {}
void IRVisitor::visitBinOpInst(const BinOpInst*) {}
void IRVisitor::visitICmpInst(const ICmpInst*) {}
void IRVisitor::visitCallInst(const CallInst*) {}
void IRVisitor::visitBranchInst(const BranchInst*) {}
void IRVisitor::visitReturnInst(const ReturnInst*) {}
void IRVisitor::visitSextInst(const SextInst*) {}
void IRVisitor::visitZextInst(const ZextInst*) {}

std::vector<const BasicBlock*> getPredecessors(const BasicBlock* bb) {
    std::vector<const BasicBlock*> preds;
    
    const auto& instrs = bb->getInstructions();
    for (const auto* inst : instrs) {
        if (auto* br = dynamic_cast<const BranchInst*>(inst)) {
            if (br->isConditional()) {
                preds.push_back(br->getTrueDest());
                preds.push_back(br->getFalseDest());
            } else {
                preds.push_back(br->getTrueDest());
            }
        }
    }
    
    return preds;
}

class CFGAnalysisVisitor : public IRVisitor {
public:
    struct BlockInfo {
        const BasicBlock* block;
        std::vector<const BasicBlock*> predecessors;
        std::vector<const BasicBlock*> successors;
        bool visited = false;
    };
    
    explicit CFGAnalysisVisitor() {}
    
    void analyze(const Module* mod);
    void analyze(const Function* func);
    
    const std::vector<BlockInfo>& getBlockInfos() const { return blockInfos_; }
    const BlockInfo* getBlockInfo(const BasicBlock* bb) const;
    
private:
    std::vector<BlockInfo> blockInfos_;
    std::unordered_map<const BasicBlock*, size_t> blockIndex_;
    
    void visitFunction(const Function* func) override;
    void visitBranchInst(const BranchInst* inst) override;
    void visitReturnInst(const ReturnInst* inst) override;
};

void CFGAnalysisVisitor::analyze(const Module* mod) {
    for (const auto* func : mod->getFunctions()) {
        analyze(func);
    }
}

void CFGAnalysisVisitor::analyze(const Function* func) {
    blockInfos_.clear();
    blockIndex_.clear();
    
    for (const auto* bb : func->getBasicBlocks()) {
        BlockInfo info;
        info.block = bb;
        blockIndex_[bb] = blockInfos_.size();
        blockInfos_.push_back(info);
    }
    
    IRVisitor::visitFunction(func);
}

const CFGAnalysisVisitor::BlockInfo* CFGAnalysisVisitor::getBlockInfo(const BasicBlock* bb) const {
    auto it = blockIndex_.find(bb);
    if (it != blockIndex_.end()) {
        return &blockInfos_[it->second];
    }
    return nullptr;
}

void CFGAnalysisVisitor::visitFunction(const Function* func) {
    for (const auto* bb : func->getBasicBlocks()) {
        auto it = blockIndex_.find(bb);
        if (it != blockIndex_.end()) {
            blockInfos_[it->second].visited = true;
        }
    }
    IRVisitor::visitFunction(func);
}

void CFGAnalysisVisitor::visitBranchInst(const BranchInst* inst) {
    if (auto* bb = inst->getParent()) {
        auto it = blockIndex_.find(bb);
        if (it != blockIndex_.end()) {
            auto& info = blockInfos_[it->second];
            
            if (inst->isConditional()) {
                info.successors.push_back(inst->getTrueDest());
                info.successors.push_back(inst->getFalseDest());
                
                auto trueIt = blockIndex_.find(inst->getTrueDest());
                auto falseIt = blockIndex_.find(inst->getFalseDest());
                if (trueIt != blockIndex_.end()) {
                    blockInfos_[trueIt->second].predecessors.push_back(bb);
                }
                if (falseIt != blockIndex_.end()) {
                    blockInfos_[falseIt->second].predecessors.push_back(bb);
                }
            } else {
                info.successors.push_back(inst->getTrueDest());
                
                auto destIt = blockIndex_.find(inst->getTrueDest());
                if (destIt != blockIndex_.end()) {
                    blockInfos_[destIt->second].predecessors.push_back(bb);
                }
            }
        }
    }
}

void CFGAnalysisVisitor::visitReturnInst(const ReturnInst*) {
}

class InstructionCountVisitor : public IRVisitor {
public:
    InstructionCountVisitor() {}
    
    void reset() {
        counts_.clear();
    }
    
    void analyze(const Module* mod);
    void analyze(const Function* func);
    
    size_t getCount(OpCode opcode) const {
        auto it = counts_.find(opcode);
        return it != counts_.end() ? it->second : 0;
    }
    
    size_t getTotalCount() const {
        size_t total = 0;
        for (const auto& pair : counts_) {
            total += pair.second;
        }
        return total;
    }
    
    const std::unordered_map<OpCode, size_t>& getCounts() const { return counts_; }
    
private:
    std::unordered_map<OpCode, size_t> counts_;
    
    void visitInstruction(const Instruction* inst) override {
        counts_[inst->getOpCode()]++;
        IRVisitor::visitInstruction(inst);
    }
};

void InstructionCountVisitor::analyze(const Module* mod) {
    reset();
    visitModule(mod);
}

void InstructionCountVisitor::analyze(const Function* func) {
    reset();
    visitFunction(func);
}

class DominatorAnalysisVisitor : public IRVisitor {
public:
    explicit DominatorAnalysisVisitor() {}
    
    void analyze(const Function* func);
    
    bool dominates(const BasicBlock* a, const BasicBlock* b) const;
    const BasicBlock* getImmediateDominator(const BasicBlock* bb) const;
    
private:
    std::unordered_map<const BasicBlock*, std::vector<const BasicBlock*> > dominators_;
    std::unordered_map<const BasicBlock*, const BasicBlock*> idom_;
    const Function* currentFunc_ = nullptr;
    
    void computeDominators(const Function* func);
};

void DominatorAnalysisVisitor::analyze(const Function* func) {
    currentFunc_ = func;
    dominators_.clear();
    idom_.clear();
    computeDominators(func);
}

void DominatorAnalysisVisitor::computeDominators(const Function* func) {
    const auto& blocks = func->getBasicBlocks();
    if (blocks.empty()) return;
    
    const BasicBlock* entry = blocks.front();
    std::vector<const BasicBlock*> entryDom;
    entryDom.push_back(entry);
    dominators_[entry] = entryDom;
    
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (size_t i = 1; i < blocks.size(); ++i) {
            const BasicBlock* bb = blocks[i];
            
            std::vector<const BasicBlock*> newDom;
            newDom.push_back(bb);
            
            for (const auto* pred : getPredecessors(bb)) {
                if (dominators_.find(pred) != dominators_.end()) {
                    if (newDom.empty()) {
                        newDom = dominators_[pred];
                    } else {
                        std::vector<const BasicBlock*> intersect;
                        for (size_t j = 0; j < newDom.size(); ++j) {
                            for (const auto* d : dominators_[pred]) {
                                if (newDom[j] == d) {
                                    intersect.push_back(d);
                                    break;
                                }
                            }
                        }
                        newDom = std::move(intersect);
                    }
                }
            }
            
            if (dominators_[bb] != newDom) {
                dominators_[bb] = newDom;
                changed = true;
            }
        }
    }
}

bool DominatorAnalysisVisitor::dominates(const BasicBlock* a, const BasicBlock* b) const {
    auto it = dominators_.find(b);
    if (it == dominators_.end()) return false;
    
    for (const auto* dom : it->second) {
        if (dom == a) return true;
    }
    return false;
}

const BasicBlock* DominatorAnalysisVisitor::getImmediateDominator(const BasicBlock* bb) const {
    auto it = dominators_.find(bb);
    if (it == dominators_.end()) return nullptr;
    
    const auto& doms = it->second;
    if (doms.size() <= 1) return nullptr;
    
    for (size_t i = 1; i < doms.size(); ++i) {
        bool isIDom = true;
        for (size_t j = 1; j < doms.size(); ++j) {
            if (i != j && dominates(doms[j], doms[i])) {
                isIDom = false;
                break;
            }
        }
        if (isIDom) return doms[i];
    }
    
    return nullptr;
}

}

#endif
