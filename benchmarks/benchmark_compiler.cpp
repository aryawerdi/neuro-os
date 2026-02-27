#include <benchmark/benchmark.h>
#include "neuro_os/compiler/ir_builder.hpp"
#include "neuro_os/utils/thread_pool.hpp"

static void BM_IRBuilder_CreateModule(benchmark::State& state) {
    for (auto _ : state) {
        auto module = std::make_shared<neuro_os::compiler::IRModule>();
        benchmark::DoNotOptimize(module);
    }
}
BENCHMARK(BM_IRBuilder_CreateModule);

static void BM_IRBuilder_CreateFunction(benchmark::State& state) {
    neuro_os::compiler::IRBuilder builder;
    for (auto _ : state) {
        auto func = builder.create_function("test_func", neuro_os::compiler::IRType::I32);
        benchmark::DoNotOptimize(func);
    }
}
BENCHMARK(BM_IRBuilder_CreateFunction);

static void BM_IRBuilder_CreateBasicBlock(benchmark::State& state) {
    neuro_os::compiler::IRBuilder builder;
    for (auto _ : state) {
        auto block = builder.create_basic_block("entry");
        benchmark::DoNotOptimize(block);
    }
}
BENCHMARK(BM_IRBuilder_CreateBasicBlock);

static void BM_IRBuilder_CreateInstructions(benchmark::State& state) {
    neuro_os::compiler::IRBuilder builder;
    auto func = builder.create_function("test", neuro_os::compiler::IRType::I32);
    auto block = builder.create_basic_block("entry");
    builder.set_insert_block(block);
    
    for (auto _ : state) {
        auto alloca = builder.create_alloca(neuro_os::compiler::IRType::I32, "x");
        benchmark::DoNotOptimize(alloca);
    }
}
BENCHMARK(BM_IRBuilder_CreateInstructions);

static void BM_IRBuilder_MultipleFunctions(benchmark::State& state) {
    neuro_os::compiler::IRBuilder builder;
    auto module = std::make_shared<neuro_os::compiler::IRModule>();
    builder = neuro_os::compiler::IRBuilder(module);
    
    for (auto _ : state) {
        for (int i = 0; i < state.range(0); ++i) {
            auto func = builder.create_function("func_" + std::to_string(i), neuro_os::compiler::IRType::I32);
            auto block = builder.create_basic_block("entry");
            builder.set_insert_block(block);
            builder.create_ret_void();
        }
    }
}
BENCHMARK(BM_IRBuilder_MultipleFunctions)->Range(1, 100);

static void BM_ThreadPool_Submit(benchmark::State& state) {
    neuro_os::ThreadPool pool(4);
    for (auto _ : state) {
        auto future = pool.submit([]() { return 42; });
        future.get();
    }
}
BENCHMARK(BM_ThreadPool_Submit);

static void BM_ThreadPool_BatchSubmit(benchmark::State& state) {
    neuro_os::ThreadPool pool(4);
    for (auto _ : state) {
        std::vector<std::future<int>> futures;
        for (int i = 0; i < state.range(0); ++i) {
            futures.push_back(pool.submit([i]() { return i; }));
        }
        for (auto& f : futures) {
            f.get();
        }
    }
}
BENCHMARK(BM_ThreadPool_BatchSubmit)->Range(1, 100);

BENCHMARK_MAIN();
