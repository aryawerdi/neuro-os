#ifndef NEURO_OS_COMPILER_IR_ANALYSIS_HPP
#define NEURO_OS_COMPILER_IR_ANALYSIS_HPP

#include "ir_builder.hpp"
#include "ir_ssa.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <queue>

namespace neuro_os::compiler {

class ControlFlowGraph {
public:
    struct Node {
        BasicBlock* block;
        std::vector<Node*> predecessors;
        std::vector<Node*> successors;
        size_t id;
    };
    
    explicit ControlFlowGraph(Function* func);
    
    Node* getNode(BasicBlock* bb) const;
    const std::vector<Node*>& getNodes() const { return nodes_; }
    Node* getEntryNode() const { return entry_node_; }
    
    void computeDominanceTree();
    void computeDominanceFrontier();
    void computeLoops();
    
    bool dominates(Node* a, Node* b) const;
    Node* getImmediateDominator(Node* node) const;
    const std::vector<Node*>& getDominanceFrontier(Node* node) const;
    
    struct LoopInfo {
        Node* header;
        std::unordered_set<Node*> blocks;
        std::vector<Node*> exits;
        std::vector<Node*> latches;
        LoopInfo* parent = nullptr;
        std::vector<LoopInfo*> children;
    };
    
    const std::vector<LoopInfo*>& getLoops() const { return loops_; }
    
private:
    Function* func_;
    std::vector<Node*> nodes_;
    std::unordered_map<BasicBlock*, Node*> block_to_node_;
    Node* entry_node_ = nullptr;
    
    std::unordered_map<Node*, std::unordered_set<Node*> > dominators_;
    std::unordered_map<Node*, Node*> idom_;
    std::unordered_map<Node*, std::vector<Node*> > dominance_frontier_;
    std::vector<LoopInfo*> loops_;
    
    void buildGraph();
    void computeDominators();
    void computeDominanceFrontierForNode(Node* node);
    void findNaturalLoops();
    void buildLoopNestingTree();
};

ControlFlowGraph::ControlFlowGraph(Function* func) : func_(func) {
    buildGraph();
    computeDominanceTree();
    computeDominanceFrontier();
    computeLoops();
}

void ControlFlowGraph::buildGraph() {
    nodes_.clear();
    block_to_node_.clear();
    
    for (auto* bb : func_->getBasicBlocks()) {
        auto* node = new Node();
        node->block = bb;
        node->id = nodes_.size();
        nodes_.push_back(node);
        block_to_node_[bb] = node;
    }
    
    for (auto* node : nodes_) {
        auto* terminator = node->block->getTerminator();
        if (auto* br = dynamic_cast<BranchInst*>(terminator)) {
            if (br->isConditional()) {
                auto* true_node = block_to_node_[br->getTrueDest()];
                auto* false_node = block_to_node_[br->getFalseDest()];
                node->successors.push_back(true_node);
                node->successors.push_back(false_node);
                true_node->predecessors.push_back(node);
                false_node->predecessors.push_back(node);
            } else {
                auto* dest_node = block_to_node_[br->getTrueDest()];
                node->successors.push_back(dest_node);
                dest_node->predecessors.push_back(node);
            }
        }
    }
    
    if (!nodes_.empty()) {
        entry_node_ = nodes_.front();
    }
}

ControlFlowGraph::Node* ControlFlowGraph::getNode(BasicBlock* bb) const {
    auto it = block_to_node_.find(bb);
    return it != block_to_node_.end() ? it->second : nullptr;
}

void ControlFlowGraph::computeDominanceTree() {
    computeDominators();
    
    idom_.clear();
    for (auto* node : nodes_) {
        auto& doms = dominators_[node];
        for (auto* dom : doms) {
            if (dom != node) {
                bool is_idom = true;
                for (auto* other : doms) {
                    if (other != node && other != dom && dominates(other, dom)) {
                        is_idom = false;
                        break;
                    }
                }
                if (is_idom) {
                    idom_[node] = dom;
                    break;
                }
            }
        }
    }
}

void ControlFlowGraph::computeDominators() {
    dominators_.clear();
    
    for (auto* node : nodes_) {
        std::unordered_set<Node*> all_nodes(nodes_.begin(), nodes_.end());
        dominators_[node] = all_nodes;
    }
    
    if (entry_node_) {
        dominators_[entry_node_].clear();
        dominators_[entry_node_].insert(entry_node_);
    }
    
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (auto* node : nodes_) {
            if (node == entry_node_) continue;
            
            auto& current_doms = dominators_[node];
            std::unordered_set<Node*> new_doms;
            new_doms.insert(node);
            
            if (!node->predecessors.empty()) {
                new_doms.insert(dominators_[node->predecessors[0]].begin(), 
                               dominators_[node->predecessors[0]].end());
                
                for (size_t i = 1; i < node->predecessors.size(); ++i) {
                    std::unordered_set<Node*> intersect;
                    for (auto* dom : new_doms) {
                        if (dominators_[node->predecessors[i]].find(dom) != 
                            dominators_[node->predecessors[i]].end()) {
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

bool ControlFlowGraph::dominates(Node* a, Node* b) const {
    auto it = dominators_.find(b);
    if (it == dominators_.end()) return false;
    
    return it->second.find(a) != it->second.end();
}

ControlFlowGraph::Node* ControlFlowGraph::getImmediateDominator(Node* node) const {
    auto it = idom_.find(node);
    return it != idom_.end() ? it->second : nullptr;
}

void ControlFlowGraph::computeDominanceFrontier() {
    dominance_frontier_.clear();
    
    for (auto* node : nodes_) {
        computeDominanceFrontierForNode(node);
    }
}

void ControlFlowGraph::computeDominanceFrontierForNode(Node* node) {
    auto& frontier = dominance_frontier_[node];
    frontier.clear();
    
    if (node->predecessors.size() > 1) {
        for (auto* pred : node->predecessors) {
            auto* runner = pred;
            while (runner && runner != idom_[node]) {
                dominance_frontier_[runner].push_back(node);
                runner = idom_[runner];
            }
        }
    }
}

const std::vector<ControlFlowGraph::Node*>& ControlFlowGraph::getDominanceFrontier(Node* node) const {
    static std::vector<Node*> empty;
    auto it = dominance_frontier_.find(node);
    return it != dominance_frontier_.end() ? it->second : empty;
}

void ControlFlowGraph::computeLoops() {
    findNaturalLoops();
    buildLoopNestingTree();
}

void ControlFlowGraph::findNaturalLoops() {
    for (auto* node : nodes_) {
        for (auto* succ : node->successors) {
            if (dominates(succ, node)) {
                auto* loop = new LoopInfo;
                loop->header = succ;
                loop->blocks.insert(succ);
                loop->blocks.insert(node);
                
                std::queue<Node*> worklist;
                worklist.push(node);
                
                while (!worklist.empty()) {
                    auto* current = worklist.front();
                    worklist.pop();
                    
                    for (auto* pred : current->predecessors) {
                        if (loop->blocks.find(pred) == loop->blocks.end()) {
                            loop->blocks.insert(pred);
                            worklist.push(pred);
                        }
                    }
                }
                
                for (auto* block : loop->blocks) {
                    for (auto* succ_block : block->successors) {
                        if (loop->blocks.find(succ_block) == loop->blocks.end()) {
                            loop->exits.push_back(succ_block);
                        }
                    }
                    
                    for (auto* pred_block : block->predecessors) {
                        if (loop->blocks.find(pred_block) != loop->blocks.end() && 
                            block == loop->header) {
                            loop->latches.push_back(pred_block);
                        }
                    }
                }
                
                loops_.push_back(loop);
            }
        }
    }
}

void ControlFlowGraph::buildLoopNestingTree() {
    std::sort(loops_.begin(), loops_.end(), 
              [](LoopInfo* a, LoopInfo* b) -> bool {
                  return a->blocks.size() > b->blocks.size();
              });
    
    for (size_t i = 0; i < loops_.size(); ++i) {
        for (size_t j = i + 1; j < loops_.size(); ++j) {
            if (loops_[i]->blocks.size() > loops_[j]->blocks.size()) {
                bool contains = true;
                for (auto* block : loops_[j]->blocks) {
                    if (loops_[i]->blocks.find(block) == loops_[i]->blocks.end()) {
                        contains = false;
                        break;
                    }
                }
                if (contains) {
                    loops_[j]->parent = loops_[i];
                    loops_[i]->children.push_back(loops_[j]);
                }
            }
        }
    }
}

class DataFlowAnalysis {
public:
    enum class Direction {
        Forward,
        Backward
    };
    
    struct AnalysisResult {
        std::unordered_map<BasicBlock*, std::unordered_set<Value*> > in_sets;
        std::unordered_map<BasicBlock*, std::unordered_set<Value*> > out_sets;
        std::unordered_map<BasicBlock*, std::unordered_set<Value*> > gen_sets;
        std::unordered_map<BasicBlock*, std::unordered_set<Value*> > kill_sets;
    };
    
    explicit DataFlowAnalysis(Function* func);
    
    AnalysisResult computeReachingDefinitions();
    AnalysisResult computeLiveVariables();
    AnalysisResult computeAvailableExpressions();
    
private:
    Function* func_;
    ControlFlowGraph cfg_;
    
    void computeGenKillSets(AnalysisResult& result);
    void computeGenKillForBlock(BasicBlock* bb, AnalysisResult& result);
    
    template<typename T>
    void solveDataFlowEquations(AnalysisResult& result, Direction dir);
};

DataFlowAnalysis::DataFlowAnalysis(Function* func) 
    : func_(func), cfg_(func) {}

DataFlowAnalysis::AnalysisResult DataFlowAnalysis::computeReachingDefinitions() {
    AnalysisResult result;
    computeGenKillSets(result);
    solveDataFlowEquations<std::unordered_set<Value*> >(result, Direction::Forward);
    return result;
}

DataFlowAnalysis::AnalysisResult DataFlowAnalysis::computeLiveVariables() {
    AnalysisResult result;
    computeGenKillSets(result);
    solveDataFlowEquations<std::unordered_set<Value*> >(result, Direction::Backward);
    return result;
}

DataFlowAnalysis::AnalysisResult DataFlowAnalysis::computeAvailableExpressions() {
    AnalysisResult result;
    computeGenKillSets(result);
    solveDataFlowEquations<std::unordered_set<Value*> >(result, Direction::Forward);
    return result;
}

void DataFlowAnalysis::computeGenKillSets(AnalysisResult& result) {
    for (auto* bb : func_->getBasicBlocks()) {
        computeGenKillForBlock(bb, result);
    }
}

void DataFlowAnalysis::computeGenKillForBlock(BasicBlock* bb, AnalysisResult& result) {
    auto& gen = result.gen_sets[bb];
    auto& kill = result.kill_sets[bb];
    
    for (auto* inst : bb->getInstructions()) {
        if (!inst->getName().empty()) {
            gen.insert(inst);
        }
        
        for (size_t i = 0; i < inst->getNumOperands(); ++i) {
            if (auto* named_val = dynamic_cast<Instruction*>(inst->getOperand(i))) {
                if (!named_val->getName().empty()) {
                    kill.insert(named_val);
                }
            }
        }
    }
}

template<typename T>
void DataFlowAnalysis::solveDataFlowEquations(AnalysisResult& result, Direction dir) {
    bool changed = true;
    while (changed) {
        changed = false;
        
        auto process_block = [&](BasicBlock* bb) -> void {
            auto& gen = result.gen_sets[bb];
            auto& kill = result.kill_sets[bb];
            auto& in = result.in_sets[bb];
            auto& out = result.out_sets[bb];
            
            T new_in;
            if (dir == Direction::Forward) {
                auto* node = cfg_.getNode(bb);
                for (auto* pred : node->predecessors) {
                    new_in.insert(result.out_sets[pred->block].begin(),
                                 result.out_sets[pred->block].end());
                }
            } else {
                auto* node = cfg_.getNode(bb);
                for (auto* succ : node->successors) {
                    new_in.insert(result.in_sets[succ->block].begin(),
                                 result.in_sets[succ->block].end());
                }
            }
            
            if (in != new_in) {
                in = std::move(new_in);
                changed = true;
            }
            
            T new_out;
            if (dir == Direction::Forward) {
                std::set_difference(in.begin(), in.end(), 
                                   kill.begin(), kill.end(),
                                   std::inserter(new_out, new_out.begin()));
                new_out.insert(gen.begin(), gen.end());
            } else {
                std::set_difference(out.begin(), out.end(),
                                   kill.begin(), kill.end(),
                                   std::inserter(new_out, new_out.begin()));
                new_out.insert(gen.begin(), gen.end());
            }
            
            if (out != new_out) {
                out = std::move(new_out);
                changed = true;
            }
        };
        
        if (dir == Direction::Forward) {
            for (auto* bb : func_->getBasicBlocks()) {
                process_block(bb);
            }
        } else {
            for (auto it = func_->getBasicBlocks().rbegin(); 
                 it != func_->getBasicBlocks().rend(); ++it) {
                process_block(*it);
            }
        }
    }
}

}

#endif