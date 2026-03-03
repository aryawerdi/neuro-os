# NeuroOS JIT IR System

Enhanced Intermediate Representation (IR) system for the NeuroOS JIT compiler with SSA support, advanced analysis, optimization passes, and serialization.

## Overview

This IR system provides a comprehensive framework for building, analyzing, optimizing, and serializing intermediate representations for just-in-time compilation. It follows LLVM-inspired design patterns with modern C++20 features.

## Features

### 1. **Enhanced IR Builder** (`ir_builder.hpp`)
- Basic IR construction with instructions, basic blocks, functions, and modules
- Support for common operations: arithmetic, memory, control flow
- Type system with integers, floats, pointers, and labels
- Instruction categories: terminators, binary ops, memory ops

### 2. **SSA Support** (`ir_ssa.hpp`)
- **Phi nodes** for control flow merging
- **SSA conversion** algorithm with dominance frontier computation
- **Variable renaming** for SSA form
- **Dominance analysis** for phi placement

### 3. **Advanced IR Analysis** (`ir_analysis.hpp`)
- **Control Flow Graph (CFG)** analysis
- **Dominance tree** calculation
- **Loop detection** and natural loop identification
- **Data flow analysis**:
  - Reaching definitions
  - Live variables
  - Available expressions

### 4. **IR Optimization Passes** (`ir_optimization.hpp`)
- **Constant propagation**
- **Dead code elimination**
- **Common subexpression elimination**
- **Instruction combining**
- **Optimizer framework** for pass management

### 5. **IR Serialization** (`ir_serialization.hpp`)
- **Binary serialization** for modules, functions, basic blocks, instructions
- **IR verification** for correctness checking
- **Type-safe serialization** with value ID mapping

### 6. **Utilities**
- **IR Printer** (`ir_printer.hpp`) for human-readable output
- **IR Visitor** (`ir_visitor.hpp`) for pattern-based traversal
- **Control flow analysis** with predecessor/successor relationships

## Architecture

### Core Components

```
Module
├── Functions
│   ├── Arguments
│   └── BasicBlocks
│       └── Instructions
│           ├── AllocaInst
│           ├── LoadInst
│           ├── StoreInst
│           ├── BinOpInst
│           ├── ICmpInst
│           ├── BranchInst
│           ├── ReturnInst
│           ├── PhiInst (SSA)
│           └── ...
└── Type system
```

### Analysis Pipeline

1. **Build IR** → **Convert to SSA** → **Analyze CFG** → **Data Flow** → **Optimize**

### Optimization Pipeline

1. **Constant Propagation** → **Dead Code Elimination** → **CSE** → **Instruction Combining**

## Usage Examples

### Basic IR Construction
```cpp
Module mod("example");
IRBuilder builder(&mod);

Function* func = builder.CreateFunction(Type::Int32, "add");
builder.setInsertPoint(func->CreateBasicBlock("entry"));

AllocaInst* a = builder.CreateAlloca(Type::Int32, "a");
AllocaInst* b = builder.CreateAlloca(Type::Int32, "b");

Value* sum = builder.CreateAdd(
    builder.CreateLoad(Type::Int32, a, "load_a"),
    builder.CreateLoad(Type::Int32, b, "load_b"),
    "sum"
);

builder.CreateRet(sum);
mod.dump();
```

### SSA Conversion
```cpp
SSABuilder ssab(&mod);
ssab.convertToSSA(func);
```

### Control Flow Analysis
```cpp
ControlFlowGraph cfg(func);
for (auto* node : cfg.getNodes()) {
    std::cout << "Node: " << node->block->getName() << "\n";
    std::cout << "  Dominator: " << cfg.getImmediateDominator(node)->block->getName() << "\n";
}
```

### Optimization
```cpp
ConstantPropagation cp(func);
cp.run();

DeadCodeElimination dce(func);
dce.run();

CommonSubexpressionElimination cse(func);
cse.run();
```

### Serialization
```cpp
IRSerializer serializer(&mod);
auto serialized = serializer.serializeModule();

IRVerifier verifier(func);
if (verifier.verify()) {
    std::cout << "IR is valid\n";
}
```

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Testing

```bash
./test_ir_system
./example_ir
```

## Design Principles

1. **Extensibility**: Easy to add new instructions, analyses, and optimizations
2. **Visitor Pattern**: Clean separation between IR structure and algorithms
3. **SSA Form**: Enables advanced optimizations and analyses
4. **Type Safety**: Strong typing for IR values and operations
5. **Memory Safety**: RAII patterns and smart pointers where appropriate

## Future Enhancements

1. **More instruction types**: Vector ops, atomic ops, memory barriers
2. **Advanced optimizations**: Loop invariant code motion, vectorization
3. **Target-specific lowering**: Architecture-specific code generation
4. **JIT compilation**: Direct machine code generation
5. **Debug information**: Source mapping and debugging support

## Performance Considerations

- **SSA form** enables efficient data flow analysis
- **Dominance frontiers** computed once, reused for phi placement
- **Worklist algorithms** for iterative data flow
- **Hash-based CSE** for fast expression matching

## Dependencies

- C++20 compiler
- Standard library with `<functional>`, `<unordered_map>`, etc.
- CMake 3.20+ for building

## License

Part of the NeuroOS project - see main project license.
