#include <gtest/gtest.h>
#include "neuro_os/compiler/ir_builder.hpp"

using namespace neuro_os::compiler;

TEST(IRBuilderTest, CreateModule) {
    auto module = std::make_shared<IRModule>();
    EXPECT_NE(module, nullptr);
    module->set_source_filename("test.ll");
    EXPECT_EQ(module->get_source_filename(), "test.ll");
}

TEST(IRBuilderTest, CreateFunction) {
    IRBuilder builder;
    auto func = builder.create_function("test_func", IRType::I32);
    
    EXPECT_EQ(func->get_name(), "test_func");
    EXPECT_EQ(func->get_return_type(), IRType::I32);
}

TEST(IRBuilderTest, CreateBasicBlock) {
    IRBuilder builder;
    auto block = builder.create_basic_block("entry");
    
    EXPECT_EQ(block->get_name(), "entry");
    builder.set_insert_block(block);
    EXPECT_EQ(builder.get_insert_block(), block);
}

TEST(IRBuilderTest, CreateAllocaInstruction) {
    IRBuilder builder;
    auto module = builder.get_module();
    auto func = builder.create_function("test_func", IRType::I32);
    auto block = builder.create_basic_block("entry");
    builder.set_insert_block(block);
    
    auto alloca = builder.create_alloca(IRType::I32, "x");
    
    EXPECT_NE(alloca, nullptr);
    EXPECT_EQ(alloca->get_opcode(), IROpcode::ALLOCA);
}

TEST(IRBuilderTest, CreateLoadStoreInstructions) {
    IRBuilder builder;
    auto func = builder.create_function("test_func", IRType::I32);
    auto block = builder.create_basic_block("entry");
    builder.set_insert_block(block);
    
    auto alloca = builder.create_alloca(IRType::I32, "x");
    auto load = builder.create_load(IRType::I32, "val");
    auto store = builder.create_store(IRValue(IRType::I32, "42"), IRValue(IRType::PTR, "x"));
    
    EXPECT_NE(load, nullptr);
    EXPECT_NE(store, nullptr);
    EXPECT_EQ(load->get_opcode(), IROpcode::LOAD);
    EXPECT_EQ(store->get_opcode(), IROpcode::STORE);
}

TEST(IRBuilderTest, CreateArithmeticInstructions) {
    IRBuilder builder;
    auto func = builder.create_function("test_func", IRType::I32);
    auto block = builder.create_basic_block("entry");
    builder.set_insert_block(block);
    
    IRValue lhs(IRType::I32, "a");
    IRValue rhs(IRType::I32, "b");
    
    auto add = builder.create_add(IRType::I32, lhs, rhs, "sum");
    auto sub = builder.create_sub(IRType::I32, lhs, rhs, "diff");
    auto mul = builder.create_mul(IRType::I32, lhs, rhs, "product");
    auto div = builder.create_div(IRType::I32, lhs, rhs, "quotient");
    
    EXPECT_EQ(add->get_opcode(), IROpcode::ADD);
    EXPECT_EQ(sub->get_opcode(), IROpcode::SUB);
    EXPECT_EQ(mul->get_opcode(), IROpcode::MUL);
    EXPECT_EQ(div->get_opcode(), IROpcode::DIV);
}

TEST(IRBuilderTest, CreateCompareInstruction) {
    IRBuilder builder;
    auto func = builder.create_function("test_func", IRType::I1);
    auto block = builder.create_basic_block("entry");
    builder.set_insert_block(block);
    
    IRValue lhs(IRType::I32, "a");
    IRValue rhs(IRType::I32, "b");
    
    auto cmp = builder.create_cmp(IROpcode::CMP, IRType::I32, lhs, rhs, "cmp_result");
    
    EXPECT_EQ(cmp->get_opcode(), IROpcode::CMP);
}

TEST(IRBuilderTest, CreateBranchInstructions) {
    IRBuilder builder;
    auto func = builder.create_function("test_func", IRType::VOID);
    auto block1 = builder.create_basic_block("entry");
    auto block2 = builder.create_basic_block("then");
    auto block3 = builder.create_basic_block("end");
    builder.set_insert_block(block1);
    
    IRValue cond(IRType::I1, "condition");
    builder.create_branch(block2, block3, cond);
    builder.set_insert_block(block2);
    builder.create_unconditional_branch(block3);
    
    EXPECT_EQ(block1->get_successors().size(), 2);
    EXPECT_EQ(block2->get_successors().size(), 1);
}

TEST(IRBuilderTest, CreateReturnInstruction) {
    IRBuilder builder;
    auto func = builder.create_function("test_func", IRType::I32);
    auto block = builder.create_basic_block("entry");
    builder.set_insert_block(block);
    
    IRValue ret_val(IRType::I32, "42");
    auto ret = builder.create_ret(ret_val);
    auto ret_void = builder.create_ret_void();
    
    EXPECT_EQ(ret->get_opcode(), IROpcode::RET);
    EXPECT_EQ(ret_void->get_opcode(), IROpcode::RET);
}

TEST(IRBuilderTest, FunctionParameters) {
    IRBuilder builder;
    auto func = builder.create_function("test_func", IRType::I32);
    
    IRValue param1(IRType::I32, "a");
    IRValue param2(IRType::I32, "b");
    func->add_parameter(param1);
    func->add_parameter(param2);
    
    EXPECT_EQ(func->get_parameters().size(), 2);
}

TEST(IRBuilderTest, MultipleBasicBlocks) {
    IRBuilder builder;
    auto func = builder.create_function("test_func", IRType::I32);
    
    auto block1 = builder.create_basic_block("entry");
    auto block2 = builder.create_basic_block("loop");
    auto block3 = builder.create_basic_block("exit");
    
    EXPECT_EQ(func->get_blocks().size(), 3);
}

TEST(IRBuilderTest, IRTypeEnum) {
    EXPECT_EQ(static_cast<int>(IRType::VOID), 0);
    EXPECT_EQ(static_cast<int>(IRType::I32), 4);
    EXPECT_EQ(static_cast<int>(IRType::F64), 9);
}
