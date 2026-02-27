#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <map>
#include <optional>

namespace neuro_os::compiler {

enum class IROpcode {
    LOAD,
    STORE,
    ADD,
    SUB,
    MUL,
    DIV,
    AND,
    OR,
    XOR,
    SHL,
    SHR,
    CMP,
    BRANCH,
    CALL,
    RET,
    ALLOCA,
    PHI,
    SELECT,
    CAST
};

enum class IRType {
    VOID,
    I1,
    I8,
    I16,
    I32,
    I64,
    F32,
    F64,
    PTR,
    ARRAY,
    STRUCT
};

class IRValue {
public:
    IRValue() = default;
    explicit IRValue(IRType type, const std::string& name = "") 
        : type_(type), name_(name) {}
    
    IRType get_type() const { return type_; }
    void set_type(IRType type) { type_ = type; }
    
    const std::string& get_name() const { return name_; }
    void set_name(const std::string& name) { name_ = name; }
    
private:
    IRType type_;
    std::string name_;
};

class IRInstruction : public IRValue {
public:
    IRInstruction(IROpcode opcode, IRType result_type, const std::string& name = "")
        : IRValue(result_type, name), opcode_(opcode) {}
    
    IROpcode get_opcode() const { return opcode_; }
    
    void add_operand(const IRValue& value) { operands_.push_back(value); }
    const std::vector<IRValue>& get_operands() const { return operands_; }
    
private:
    IROpcode opcode_;
    std::vector<IRValue> operands_;
};

class IRBasicBlock {
public:
    explicit IRBasicBlock(const std::string& name = "") : name_(name) {}
    
    void add_instruction(std::shared_ptr<IRInstruction> inst) {
        instructions_.push_back(inst);
    }
    
    const std::string& get_name() const { return name_; }
    const std::vector<std::shared_ptr<IRInstruction>>& get_instructions() const {
        return instructions_;
    }
    
    void set_predecessor(IRBasicBlock* pred) { predecessors_.push_back(pred); }
    void set_successor(IRBasicBlock* succ) { successors_.push_back(succ); }
    
    const std::vector<IRBasicBlock*>& get_predecessors() const { return predecessors_; }
    const std::vector<IRBasicBlock*>& get_successors() const { return successors_; }
    
private:
    std::string name_;
    std::vector<std::shared_ptr<IRInstruction>> instructions_;
    std::vector<IRBasicBlock*> predecessors_;
    std::vector<IRBasicBlock*> successors_;
};

class IRFunction {
public:
    explicit IRFunction(const std::string& name) : name_(name) {}
    
    void add_basic_block(std::shared_ptr<IRBasicBlock> block) {
        blocks_.push_back(block);
    }
    
    const std::string& get_name() const { return name_; }
    const std::vector<std::shared_ptr<IRBasicBlock>>& get_blocks() const { return blocks_; }
    
    void add_parameter(const IRValue& param) { parameters_.push_back(param); }
    const std::vector<IRValue>& get_parameters() const { return parameters_; }
    
    void set_return_type(IRType type) { return_type_ = type; }
    IRType get_return_type() const { return return_type_; }
    
private:
    std::string name_;
    std::vector<std::shared_ptr<IRBasicBlock>> blocks_;
    std::vector<IRValue> parameters_;
    IRType return_type_ = IRType::VOID;
};

class IRModule {
public:
    void add_function(std::shared_ptr<IRFunction> func) {
        functions_.push_back(func);
    }
    
    const std::vector<std::shared_ptr<IRFunction>>& get_functions() const {
        return functions_;
    }
    
    void set_source_filename(const std::string& filename) {
        source_filename_ = filename;
    }
    
    const std::string& get_source_filename() const { return source_filename_; }
    
    std::string to_string() const;

private:
    std::vector<std::shared_ptr<IRFunction>> functions_;
    std::string source_filename_;
};

class IRBuilder {
public:
    IRBuilder() = default;
    explicit IRBuilder(std::shared_ptr<IRModule> module) : module_(module) {}
    
    void set_insert_block(std::shared_ptr<IRBasicBlock> block) {
        current_block_ = block;
    }
    
    std::shared_ptr<IRBasicBlock> get_insert_block() const {
        return current_block_;
    }
    
    std::shared_ptr<IRInstruction> create_load(IRType type, const std::string& name = "");
    std::shared_ptr<IRInstruction> create_store(const IRValue& value, const IRValue& ptr);
    std::shared_ptr<IRInstruction> create_add(IRType type, const IRValue& lhs, const IRValue& rhs, const std::string& name = "");
    std::shared_ptr<IRInstruction> create_sub(IRType type, const IRValue& lhs, const IRValue& rhs, const std::string& name = "");
    std::shared_ptr<IRInstruction> create_mul(IRType type, const IRValue& lhs, const IRValue& rhs, const std::string& name = "");
    std::shared_ptr<IRInstruction> create_div(IRType type, const IRValue& lhs, const IRValue& rhs, const std::string& name = "");
    std::shared_ptr<IRInstruction> create_cmp(IROpcode cmp_type, IRType type, const IRValue& lhs, const IRValue& rhs, const std::string& name = "");
    std::shared_ptr<IRInstruction> create_branch(std::shared_ptr<IRBasicBlock> true_block, std::shared_ptr<IRBasicBlock> false_block, const IRValue& cond);
    std::shared_ptr<IRInstruction> create_unconditional_branch(std::shared_ptr<IRBasicBlock> block);
    std::shared_ptr<IRInstruction> create_ret(const IRValue& value);
    std::shared_ptr<IRInstruction> create_ret_void();
    std::shared_ptr<IRInstruction> create_alloca(IRType type, const std::string& name = "");
    std::shared_ptr<IRInstruction> create_call(std::shared_ptr<IRFunction> func, const std::vector<IRValue>& args, const std::string& name = "");
    
    std::shared_ptr<IRBasicBlock> create_basic_block(const std::string& name = "");
    std::shared_ptr<IRFunction> create_function(const std::string& name, IRType return_type);
    
    std::shared_ptr<IRModule> get_module() const { return module_; }

private:
    std::shared_ptr<IRModule> module_;
    std::shared_ptr<IRBasicBlock> current_block_;
    int unnamed_value_counter_ = 0;
    
    std::string generate_unnamed_name() {
        return "%" + std::to_string(unnamed_value_counter_++);
    }
};

}
