#include "neuro_os/compiler/ir_builder.hpp"
#include "neuro_os/compiler/ir_printer.hpp"
#include "neuro_os/compiler/ir_ssa.hpp"
#include "neuro_os/compiler/ir_analysis.hpp"
#include "neuro_os/compiler/ir_optimization.hpp"
#include "neuro_os/compiler/ir_serialization.hpp"
#include <iostream>

using namespace neuro_os::compiler;

void testBasicIR() {
    std::cout << "=== Testing Basic IR ===\n";
    
    Module mod("test_module");
    IRBuilder builder(&mod);
    
    Function* func = builder.CreateFunction(Type::Int32, "test_function");
    builder.setInsertPoint(func->CreateBasicBlock("entry"));
    
    AllocaInst* a = builder.CreateAlloca(Type::Int32, "a");
    AllocaInst* b = builder.CreateAlloca(Type::Int32, "b");
    
    StoreInst* store_a = builder.CreateStore(builder.CreateAdd(
        builder.CreateLoad(Type::Int32, a, "load_a"),
        builder.CreateLoad(Type::Int32, b, "load_b"),
        "add_result"
    ), a);
    
    BasicBlock* true_block = builder.CreateBasicBlock("true_block", func);
    BasicBlock* false_block = builder.CreateBasicBlock("false_block", func);
    BasicBlock* merge_block = builder.CreateBasicBlock("merge_block", func);
    
    builder.setInsertPoint(func->getEntryBlock());
    ICmpInst* cmp = builder.CreateICmp(ComparePredicate::Sgt,
        builder.CreateLoad(Type::Int32, a, "cmp_load"),
        builder.CreateLoad(Type::Int32, b, "cmp_load_b"),
        "cmp_result");
    
    BranchInst* br = builder.CreateCondBr(cmp, true_block, false_block);
    
    builder.setInsertPoint(true_block);
    StoreInst* store_true = builder.CreateStore(
        builder.CreateAdd(
            builder.CreateLoad(Type::Int32, a, "true_load"),
            builder.CreateLoad(Type::Int32, b, "true_load_b"),
            "true_add"
        ),
        a
    );
    builder.CreateBr(merge_block);
    
    builder.setInsertPoint(false_block);
    StoreInst* store_false = builder.CreateStore(
        builder.CreateSub(
            builder.CreateLoad(Type::Int32, a, "false_load"),
            builder.CreateLoad(Type::Int32, b, "false_load_b"),
            "false_sub"
        ),
        a
    );
    builder.CreateBr(merge_block);
    
    builder.setInsertPoint(merge_block);
    builder.CreateRet(builder.CreateLoad(Type::Int32, a, "ret_load"));
    
    mod.dump();
}

void testSSAConversion() {
    std::cout << "\n=== Testing SSA Conversion ===\n";
    
    Module mod("ssa_module");
    IRBuilder builder(&mod);
    
    Function* func = builder.CreateFunction(Type::Int32, "ssa_function");
    builder.setInsertPoint(func->CreateBasicBlock("entry"));
    
    AllocaInst* x = builder.CreateAlloca(Type::Int32, "x");
    AllocaInst* y = builder.CreateAlloca(Type::Int32, "y");
    
    builder.CreateStore(builder.CreateAdd(
        builder.CreateLoad(Type::Int32, x, "load_x"),
        builder.CreateLoad(Type::Int32, y, "load_y"),
        "init_add"
    ), x);
    
    BasicBlock* loop_header = builder.CreateBasicBlock("loop_header", func);
    BasicBlock* loop_body = builder.CreateBasicBlock("loop_body", func);
    BasicBlock* loop_exit = builder.CreateBasicBlock("loop_exit", func);
    
    builder.setInsertPoint(func->getEntryBlock());
    builder.CreateBr(loop_header);
    
    builder.setInsertPoint(loop_header);
    ICmpInst* loop_cmp = builder.CreateICmp(ComparePredicate::Ult,
        builder.CreateLoad(Type::Int32, x, "loop_load_x"),
        builder.CreateLoad(Type::Int32, y, "loop_load_y"),
        "loop_cmp");
    builder.CreateCondBr(loop_cmp, loop_body, loop_exit);
    
    builder.setInsertPoint(loop_body);
    builder.CreateStore(
        builder.CreateAdd(
            builder.CreateLoad(Type::Int32, x, "body_load_x"),
            builder.CreateLoad(Type::Int32, y, "body_load_y"),
            "body_add"
        ),
        x
    );
    builder.CreateBr(loop_header);
    
    builder.setInsertPoint(loop_exit);
    builder.CreateRet(builder.CreateLoad(Type::Int32, x, "exit_load"));
    
    std::cout << "Before SSA conversion:\n";
    mod.dump();
    
    SSABuilder ssab(&mod);
    ssab.convertToSSA(func);
    
    std::cout << "\nAfter SSA conversion:\n";
    mod.dump();
}

void testControlFlowAnalysis() {
    std::cout << "\n=== Testing Control Flow Analysis ===\n";
    
    Module mod("cfg_module");
    IRBuilder builder(&mod);
    
    Function* func = builder.CreateFunction(Type::Int32, "cfg_function");
    
    BasicBlock* entry = builder.CreateBasicBlock("entry", func);
    BasicBlock* bb1 = builder.CreateBasicBlock("bb1", func);
    BasicBlock* bb2 = builder.CreateBasicBlock("bb2", func);
    BasicBlock* bb3 = builder.CreateBasicBlock("bb3", func);
    BasicBlock* bb4 = builder.CreateBasicBlock("bb4", func);
    BasicBlock* exit = builder.CreateBasicBlock("exit", func);
    
    builder.setInsertPoint(entry);
    AllocaInst* a = builder.CreateAlloca(Type::Int32, "a");
    AllocaInst* b = builder.CreateAlloca(Type::Int32, "b");
    builder.CreateStore(builder.CreateAdd(
        builder.CreateLoad(Type::Int32, a, "load_a"),
        builder.CreateLoad(Type::Int32, b, "load_b"),
        "entry_add"
    ), a);
    builder.CreateCondBr(
        builder.CreateICmp(ComparePredicate::Sgt,
            builder.CreateLoad(Type::Int32, a, "cmp_load_a"),
            builder.CreateLoad(Type::Int32, b, "cmp_load_b"),
            "entry_cmp"),
        bb1, bb2
    );
    
    builder.setInsertPoint(bb1);
    builder.CreateStore(
        builder.CreateSub(
            builder.CreateLoad(Type::Int32, a, "bb1_load_a"),
            builder.CreateLoad(Type::Int32, b, "bb1_load_b"),
            "bb1_sub"
        ),
        a
    );
    builder.CreateBr(bb3);
    
    builder.setInsertPoint(bb2);
    builder.CreateStore(
        builder.CreateMul(
            builder.CreateLoad(Type::Int32, a, "bb2_load_a"),
            builder.CreateLoad(Type::Int32, b, "bb2_load_b"),
            "bb2_mul"
        ),
        a
    );
    builder.CreateBr(bb3);
    
    builder.setInsertPoint(bb3);
    builder.CreateStore(
        builder.CreateAdd(
            builder.CreateLoad(Type::Int32, a, "bb3_load_a"),
            builder.CreateLoad(Type::Int32, b, "bb3_load_b"),
            "bb3_add"
        ),
        a
    );
    builder.CreateCondBr(
        builder.CreateICmp(ComparePredicate::Ult,
            builder.CreateLoad(Type::Int32, a, "bb3_cmp_load"),
            builder.CreateLoad(Type::Int32, b, "bb3_cmp_load_b"),
            "bb3_cmp"),
        bb4, exit
    );
    
    builder.setInsertPoint(bb4);
    builder.CreateStore(
        builder.CreateSDiv(
            builder.CreateLoad(Type::Int32, a, "bb4_load_a"),
            builder.CreateLoad(Type::Int32, b, "bb4_load_b"),
            "bb4_div"
        ),
        a
    );
    builder.CreateBr(bb3);
    
    builder.setInsertPoint(exit);
    builder.CreateRet(builder.CreateLoad(Type::Int32, a, "exit_load"));
    
    mod.dump();
    
    ControlFlowGraph cfg(func);
    std::cout << "\nControl Flow Graph Analysis:\n";
    
    for (auto* node : cfg.getNodes()) {
        std::cout << "Node " << node->block->getName() << " (ID: " << node->id << ")\n";
        std::cout << "  Predecessors: ";
        for (auto* pred : node->predecessors) {
            std::cout << pred->block->getName() << " ";
        }
        std::cout << "\n  Successors: ";
        for (auto* succ : node->successors) {
            std::cout << succ->block->getName() << " ";
        }
        std::cout << "\n";
        
        auto* idom = cfg.getImmediateDominator(node);
        if (idom) {
            std::cout << "  Immediate Dominator: " << idom->block->getName() << "\n";
        }
        
        auto& frontier = cfg.getDominanceFrontier(node);
        if (!frontier.empty()) {
            std::cout << "  Dominance Frontier: ";
            for (auto* frontier_node : frontier) {
                std::cout << frontier_node->block->getName() << " ";
            }
            std::cout << "\n";
        }
    }
    
    auto& loops = cfg.getLoops();
    if (!loops.empty()) {
        std::cout << "\nLoop Analysis:\n";
        for (auto* loop : loops) {
            std::cout << "Loop with header: " << loop->header->block->getName() << "\n";
            std::cout << "  Blocks: ";
            for (auto* block : loop->blocks) {
                std::cout << block->block->getName() << " ";
            }
            std::cout << "\n  Latches: ";
            for (auto* latch : loop->latches) {
                std::cout << latch->block->getName() << " ";
            }
            std::cout << "\n  Exits: ";
            for (auto* exit : loop->exits) {
                std::cout << exit->block->getName() << " ";
            }
            std::cout << "\n";
        }
    }
}

void testDataFlowAnalysis() {
    std::cout << "\n=== Testing Data Flow Analysis ===\n";
    
    Module mod("dfa_module");
    IRBuilder builder(&mod);
    
    Function* func = builder.CreateFunction(Type::Int32, "dfa_function");
    builder.setInsertPoint(func->CreateBasicBlock("entry"));
    
    AllocaInst* a = builder.CreateAlloca(Type::Int32, "a");
    AllocaInst* b = builder.CreateAlloca(Type::Int32, "b");
    AllocaInst* c = builder.CreateAlloca(Type::Int32, "c");
    
    builder.CreateStore(builder.CreateAdd(
        builder.CreateLoad(Type::Int32, a, "load_a"),
        builder.CreateLoad(Type::Int32, b, "load_b"),
        "init_add"
    ), c);
    
    BasicBlock* loop_header = builder.CreateBasicBlock("loop_header", func);
    BasicBlock* loop_body = builder.CreateBasicBlock("loop_body", func);
    BasicBlock* loop_exit = builder.CreateBasicBlock("loop_exit", func);
    
    builder.setInsertPoint(func->getEntryBlock());
    builder.CreateBr(loop_header);
    
    builder.setInsertPoint(loop_header);
    ICmpInst* loop_cmp = builder.CreateICmp(ComparePredicate::Ult,
        builder.CreateLoad(Type::Int32, c, "loop_load_c"),
        builder.CreateLoad(Type::Int32, b, "loop_load_b"),
        "loop_cmp");
    builder.CreateCondBr(loop_cmp, loop_body, loop_exit);
    
    builder.setInsertPoint(loop_body);
    builder.CreateStore(
        builder.CreateAdd(
            builder.CreateLoad(Type::Int32, c, "body_load_c"),
            builder.CreateLoad(Type::Int32, a, "body_load_a"),
            "body_add"
        ),
        c
    );
    builder.CreateBr(loop_header);
    
    builder.setInsertPoint(loop_exit);
    builder.CreateRet(builder.CreateLoad(Type::Int32, c, "exit_load"));
    
    mod.dump();
    
    DataFlowAnalysis dfa(func);
    auto reaching_defs = dfa.computeReachingDefinitions();
    
    std::cout << "\nReaching Definitions Analysis:\n";
    for (auto* bb : func->getBasicBlocks()) {
        std::cout << "Block: " << bb->getName() << "\n";
        
        auto& gen = reaching_defs.gen_sets[bb];
        if (!gen.empty()) {
            std::cout << "  GEN: ";
            for (auto* val : gen) {
                if (auto* inst = dynamic_cast<Instruction*>(val)) {
                    std::cout << inst->getName() << " ";
                }
            }
            std::cout << "\n";
        }
        
        auto& kill = reaching_defs.kill_sets[bb];
        if (!kill.empty()) {
            std::cout << "  KILL: ";
            for (auto* val : kill) {
                if (auto* inst = dynamic_cast<Instruction*>(val)) {
                    std::cout << inst->getName() << " ";
                }
            }
            std::cout << "\n";
        }
        
        auto& in = reaching_defs.in_sets[bb];
        if (!in.empty()) {
            std::cout << "  IN: ";
            for (auto* val : in) {
                if (auto* inst = dynamic_cast<Instruction*>(val)) {
                    std::cout << inst->getName() << " ";
                }
            }
            std::cout << "\n";
        }
        
        auto& out = reaching_defs.out_sets[bb];
        if (!out.empty()) {
            std::cout << "  OUT: ";
            for (auto* val : out) {
                if (auto* inst = dynamic_cast<Instruction*>(val)) {
                    std::cout << inst->getName() << " ";
                }
            }
            std::cout << "\n";
        }
    }
}

void testOptimization() {
    std::cout << "\n=== Testing Optimization ===\n";
    
    Module mod("opt_module");
    IRBuilder builder(&mod);
    
    Function* func = builder.CreateFunction(Type::Int32, "opt_function");
    builder.setInsertPoint(func->CreateBasicBlock("entry"));
    
    AllocaInst* a = builder.CreateAlloca(Type::Int32, "a");
    AllocaInst* b = builder.CreateAlloca(Type::Int32, "b");
    
    builder.CreateStore(builder.CreateAdd(
        builder.CreateLoad(Type::Int32, a, "load_a1"),
        builder.CreateLoad(Type::Int32, b, "load_b1"),
        "add1"
    ), a);
    
    builder.CreateStore(builder.CreateAdd(
        builder.CreateLoad(Type::Int32, a, "load_a2"),
        builder.CreateLoad(Type::Int32, b, "load_b2"),
        "add2"
    ), a);
    
    builder.CreateStore(builder.CreateAdd(
        builder.CreateLoad(Type::Int32, a, "load_a3"),
        builder.CreateLoad(Type::Int32, b, "load_b3"),
        "add3"
    ), a);
    
    builder.CreateRet(builder.CreateLoad(Type::Int32, a, "ret_load"));
    
    std::cout << "Before optimization:\n";
    mod.dump();
    
    ConstantPropagation cp(func);
    cp.run();
    
    DeadCodeElimination dce(func);
    dce.run();
    
    CommonSubexpressionElimination cse(func);
    cse.run();
    
    std::cout << "\nAfter optimization:\n";
    mod.dump();
}

void testSerialization() {
    std::cout << "\n=== Testing Serialization ===\n";
    
    Module mod("serialize_module");
    IRBuilder builder(&mod);
    
    Function* func = builder.CreateFunction(Type::Int32, "serialize_function");
    builder.setInsertPoint(func->CreateBasicBlock("entry"));
    
    AllocaInst* a = builder.CreateAlloca(Type::Int32, "a");
    AllocaInst* b = builder.CreateAlloca(Type::Int32, "b");
    
    builder.CreateStore(builder.CreateAdd(
        builder.CreateLoad(Type::Int32, a, "load_a"),
        builder.CreateLoad(Type::Int32, b, "load_b"),
        "add_result"
    ), a);
    
    builder.CreateRet(builder.CreateLoad(Type::Int32, a, "ret_load"));
    
    std::cout << "Original module:\n";
    mod.dump();
    
    IRSerializer serializer(&mod);
    auto serialized = serializer.serializeModule();
    
    std::cout << "\nSerialized size: " << serialized.size() << " bytes\n";
    
    IRVerifier verifier(func);
    if (verifier.verify()) {
        std::cout << "IR verification passed\n";
    } else {
        std::cout << "IR verification failed: " << verifier.getLastError() << "\n";
    }
}

int main() {
    std::cout << "NeuroOS JIT IR System Test Suite\n";
    std::cout << "================================\n";
    
    try {
        testBasicIR();
        testSSAConversion();
        testControlFlowAnalysis();
        testDataFlowAnalysis();
        testOptimization();
        testSerialization();
        
        std::cout << "\nAll tests completed successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}