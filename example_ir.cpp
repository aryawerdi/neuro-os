#include "neuro_os/compiler/ir_builder.hpp"
#include "neuro_os/compiler/ir_printer.hpp"
#include <iostream>

using namespace neuro_os::compiler;

int main() {
    std::cout << "NeuroOS JIT IR Example\n";
    std::cout << "======================\n\n";
    
    Module mod("example_module");
    IRBuilder builder(&mod);
    
    Function* func = builder.CreateFunction(Type::Int32, "factorial");
    builder.setInsertPoint(func->CreateBasicBlock("entry"));
    
    AllocaInst* n = builder.CreateAlloca(Type::Int32, "n");
    AllocaInst* result = builder.CreateAlloca(Type::Int32, "result");
    
    builder.CreateStore(builder.CreateLoad(Type::Int32, n, "load_n"), result);
    
    BasicBlock* loop_header = builder.CreateBasicBlock("loop_header", func);
    BasicBlock* loop_body = builder.CreateBasicBlock("loop_body", func);
    BasicBlock* loop_exit = builder.CreateBasicBlock("loop_exit", func);
    
    builder.setInsertPoint(func->getEntryBlock());
    builder.CreateBr(loop_header);
    
    builder.setInsertPoint(loop_header);
    ICmpInst* loop_cmp = builder.CreateICmp(ComparePredicate::Ugt,
        builder.CreateLoad(Type::Int32, n, "loop_load_n"),
        builder.CreateLoad(Type::Int32, result, "loop_load_result"),
        "loop_cmp");
    builder.CreateCondBr(loop_cmp, loop_body, loop_exit);
    
    builder.setInsertPoint(loop_body);
    builder.CreateStore(
        builder.CreateMul(
            builder.CreateLoad(Type::Int32, result, "body_load_result"),
            builder.CreateLoad(Type::Int32, n, "body_load_n"),
            "body_mul"
        ),
        result
    );
    
    builder.CreateStore(
        builder.CreateSub(
            builder.CreateLoad(Type::Int32, n, "body_load_n2"),
            builder.CreateLoad(Type::Int32, result, "body_load_result2"),
            "body_sub"
        ),
        n
    );
    
    builder.CreateBr(loop_header);
    
    builder.setInsertPoint(loop_exit);
    builder.CreateRet(builder.CreateLoad(Type::Int32, result, "exit_load"));
    
    std::cout << "Generated IR for factorial function:\n";
    mod.dump();
    
    return 0;
}