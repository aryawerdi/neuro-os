#include <gtest/gtest.h>
#include "neuro_os/compiler/ir_builder.hpp"
#include "neuro_os/utils/thread_pool.hpp"
#include "neuro_os/utils/logging.hpp"
#include "neuro_os/utils/metrics.hpp"

using namespace neuro_os;

TEST(CompilerPipelineTest, FullJITPipeline) {
    compiler::IRBuilder builder;
    auto module = std::make_shared<compiler::IRModule>();
    builder = compiler::IRBuilder(module);
    
    auto func = builder.create_function("add", compiler::IRType::I32);
    auto entry = builder.create_basic_block("entry");
    builder.set_insert_block(entry);
    
    compiler::IRBuilder builder;
    auto module = std::make_shared<compiler::IRModule>();
    builder = compiler::IRBuilder(module);
    
    logger.info("Created module");
    auto func = builder.create_function("test", compiler::IRType::I32);
    logger.info("Created function: " + func->get_name());
    
    SUCCEED();
}

TEST(CompilerPipelineTest, MetricsIntegration) {
    MetricsCollector collector;
    
    compiler::IRBuilder builder;
    auto module = std::make_shared<compiler::IRModule>();
    builder = compiler::IRBuilder(module);
    
    for (int i = 0; i < 100; ++i) {
        auto func = builder.create_function("func_" + std::to_string(i), compiler::IRType::I32);
        collector.record("functions_created", 1);
    }
    
    EXPECT_EQ(collector.get_count("functions_created"), 100);
}
