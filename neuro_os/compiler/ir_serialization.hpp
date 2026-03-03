#ifndef NEURO_OS_COMPILER_IR_SERIALIZATION_HPP
#define NEURO_OS_COMPILER_IR_SERIALIZATION_HPP

#include "ir_builder.hpp"
#include "ir_ssa.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace neuro_os::compiler {

class IRSerializer {
public:
    explicit IRSerializer(Module* mod = nullptr);
    
    void setModule(Module* mod) { module_ = mod; }
    
    std::vector<uint8_t> serializeModule() const;
    std::vector<uint8_t> serializeFunction(Function* func) const;
    std::vector<uint8_t> serializeBasicBlock(BasicBlock* bb) const;
    std::vector<uint8_t> serializeInstruction(Instruction* inst) const;
    
    bool deserializeModule(const std::vector<uint8_t>& data);
    Function* deserializeFunction(const std::vector<uint8_t>& data);
    BasicBlock* deserializeBasicBlock(const std::vector<uint8_t>& data);
    Instruction* deserializeInstruction(const std::vector<uint8_t>& data);
    
private:
    Module* module_ = nullptr;
    mutable std::unordered_map<Value*, uint32_t> value_ids_;
    mutable uint32_t next_value_id_ = 0;
    
    uint32_t getValueId(Value* val) const;
    Value* getValueById(uint32_t id) const;
    
    void writeUInt32(std::vector<uint8_t>& buffer, uint32_t value) const;
    void writeString(std::vector<uint8_t>& buffer, const std::string& str) const;
    void writeType(std::vector<uint8_t>& buffer, Type ty) const;
    void writeOpCode(std::vector<uint8_t>& buffer, OpCode op) const;
    
    uint32_t readUInt32(const std::vector<uint8_t>& buffer, size_t& offset) const;
    std::string readString(const std::vector<uint8_t>& buffer, size_t& offset) const;
    Type readType(const std::vector<uint8_t>& buffer, size_t& offset) const;
    OpCode readOpCode(const std::vector<uint8_t>& buffer, size_t& offset) const;
};

IRSerializer::IRSerializer(Module* mod) : module_(mod) {}

std::vector<uint8_t> IRSerializer::serializeModule() const {
    std::vector<uint8_t> buffer;
    
    value_ids_.clear();
    next_value_id_ = 0;
    
    writeString(buffer, module_->getName());
    
    uint32_t num_functions = static_cast<uint32_t>(module_->getNumFunctions());
    writeUInt32(buffer, num_functions);
    
    for (auto* func : module_->getFunctions()) {
        auto func_data = serializeFunction(func);
        writeUInt32(buffer, static_cast<uint32_t>(func_data.size()));
        buffer.insert(buffer.end(), func_data.begin(), func_data.end());
    }
    
    return buffer;
}

std::vector<uint8_t> IRSerializer::serializeFunction(Function* func) const {
    std::vector<uint8_t> buffer;
    
    writeString(buffer, func->getName());
    writeType(buffer, func->getReturnType());
    
    uint32_t num_args = static_cast<uint32_t>(func->getNumArguments());
    writeUInt32(buffer, num_args);
    
    for (auto* arg : func->getArguments()) {
        writeString(buffer, arg->getName());
        writeType(buffer, arg->getType());
    }
    
    uint32_t num_blocks = static_cast<uint32_t>(func->getNumBasicBlocks());
    writeUInt32(buffer, num_blocks);
    
    for (auto* bb : func->getBasicBlocks()) {
        auto bb_data = serializeBasicBlock(bb);
        writeUInt32(buffer, static_cast<uint32_t>(bb_data.size()));
        buffer.insert(buffer.end(), bb_data.begin(), bb_data.end());
    }
    
    return buffer;
}

std::vector<uint8_t> IRSerializer::serializeBasicBlock(BasicBlock* bb) const {
    std::vector<uint8_t> buffer;
    
    writeString(buffer, bb->getName());
    
    uint32_t num_instructions = static_cast<uint32_t>(bb->getInstructions().size());
    writeUInt32(buffer, num_instructions);
    
    for (auto* inst : bb->getInstructions()) {
        auto inst_data = serializeInstruction(inst);
        writeUInt32(buffer, static_cast<uint32_t>(inst_data.size()));
        buffer.insert(buffer.end(), inst_data.begin(), inst_data.end());
    }
    
    return buffer;
}

std::vector<uint8_t> IRSerializer::serializeInstruction(Instruction* inst) const {
    std::vector<uint8_t> buffer;
    
    writeOpCode(buffer, inst->getOpCode());
    writeType(buffer, inst->getType());
    writeString(buffer, inst->getName());
    
    uint32_t num_operands = static_cast<uint32_t>(inst->getNumOperands());
    writeUInt32(buffer, num_operands);
    
    for (size_t i = 0; i < inst->getNumOperands(); ++i) {
        auto* operand = inst->getOperand(i);
        uint32_t operand_id = getValueId(operand);
        writeUInt32(buffer, operand_id);
    }
    
    if (auto* alloca = dynamic_cast<AllocaInst*>(inst)) {
        writeType(buffer, alloca->getElementType());
    } else if (auto* icmp = dynamic_cast<ICmpInst*>(inst)) {
        writeUInt32(buffer, static_cast<uint32_t>(icmp->getPredicate()));
    } else if (auto* br = dynamic_cast<BranchInst*>(inst)) {
        if (br->isConditional()) {
            writeUInt32(buffer, 1);
            auto* cond = br->getCondition();
            auto* true_dest = br->getTrueDest();
            auto* false_dest = br->getFalseDest();
            
            writeUInt32(buffer, getValueId(cond));
            writeUInt32(buffer, getValueId(true_dest));
            writeUInt32(buffer, getValueId(false_dest));
        } else {
            writeUInt32(buffer, 0);
            auto* dest = br->getTrueDest();
            writeUInt32(buffer, getValueId(dest));
        }
    } else if (auto* ret = dynamic_cast<ReturnInst*>(inst)) {
        if (ret->hasReturnValue()) {
            writeUInt32(buffer, 1);
            auto* ret_val = ret->getReturnValue();
            writeUInt32(buffer, getValueId(ret_val));
        } else {
            writeUInt32(buffer, 0);
        }
    }
    
    return buffer;
}

uint32_t IRSerializer::getValueId(Value* val) const {
    if (!val) return 0;
    
    auto it = value_ids_.find(val);
    if (it != value_ids_.end()) {
        return it->second;
    }
    
    uint32_t id = ++next_value_id_;
    value_ids_[val] = id;
    return id;
}

void IRSerializer::writeUInt32(std::vector<uint8_t>& buffer, uint32_t value) const {
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void IRSerializer::writeString(std::vector<uint8_t>& buffer, const std::string& str) const {
    writeUInt32(buffer, static_cast<uint32_t>(str.size()));
    buffer.insert(buffer.end(), str.begin(), str.end());
}

void IRSerializer::writeType(std::vector<uint8_t>& buffer, Type ty) const {
    writeUInt32(buffer, static_cast<uint32_t>(ty));
}

void IRSerializer::writeOpCode(std::vector<uint8_t>& buffer, OpCode op) const {
    writeUInt32(buffer, static_cast<uint32_t>(op));
}

uint32_t IRSerializer::readUInt32(const std::vector<uint8_t>& buffer, size_t& offset) const {
    if (offset + 4 > buffer.size()) return 0;
    
    uint32_t value = static_cast<uint32_t>(buffer[offset]) |
                    (static_cast<uint32_t>(buffer[offset + 1]) << 8) |
                    (static_cast<uint32_t>(buffer[offset + 2]) << 16) |
                    (static_cast<uint32_t>(buffer[offset + 3]) << 24);
    offset += 4;
    return value;
}

std::string IRSerializer::readString(const std::vector<uint8_t>& buffer, size_t& offset) const {
    uint32_t length = readUInt32(buffer, offset);
    if (offset + length > buffer.size()) return "";
    
    std::string str(buffer.begin() + offset, buffer.begin() + offset + length);
    offset += length;
    return str;
}

Type IRSerializer::readType(const std::vector<uint8_t>& buffer, size_t& offset) const {
    uint32_t ty = readUInt32(buffer, offset);
    return static_cast<Type>(ty);
}

OpCode IRSerializer::readOpCode(const std::vector<uint8_t>& buffer, size_t& offset) const {
    uint32_t op = readUInt32(buffer, offset);
    return static_cast<OpCode>(op);
}

class IRVerifier {
public:
    explicit IRVerifier(Function* func = nullptr);
    
    void setFunction(Function* func) { func_ = func; }
    
    bool verify() const;
    bool verifyBasicBlock(BasicBlock* bb) const;
    bool verifyInstruction(Instruction* inst) const;
    
    std::string getLastError() const { return last_error_; }
    
private:
    Function* func_ = nullptr;
    mutable std::string last_error_;
    
    bool verifyTerminator(BasicBlock* bb) const;
    bool verifyPhiNodes(BasicBlock* bb) const;
    bool verifyOperands(Instruction* inst) const;
    bool verifyTypes(Instruction* inst) const;
};

IRVerifier::IRVerifier(Function* func) : func_(func) {}

bool IRVerifier::verify() const {
    if (!func_) {
        last_error_ = "No function to verify";
        return false;
    }
    
    for (auto* bb : func_->getBasicBlocks()) {
        if (!verifyBasicBlock(bb)) {
            return false;
        }
    }
    
    return true;
}

bool IRVerifier::verifyBasicBlock(BasicBlock* bb) const {
    if (!verifyTerminator(bb)) {
        return false;
    }
    
    if (!verifyPhiNodes(bb)) {
        return false;
    }
    
    for (auto* inst : bb->getInstructions()) {
        if (!verifyInstruction(inst)) {
            return false;
        }
    }
    
    return true;
}

bool IRVerifier::verifyInstruction(Instruction* inst) const {
    if (!verifyOperands(inst)) {
        return false;
    }
    
    if (!verifyTypes(inst)) {
        return false;
    }
    
    return true;
}

bool IRVerifier::verifyTerminator(BasicBlock* bb) const {
    auto* terminator = bb->getTerminator();
    if (!terminator) {
        last_error_ = "Basic block " + bb->getName() + " has no terminator";
        return false;
    }
    
    return true;
}

bool IRVerifier::verifyPhiNodes(BasicBlock* bb) const {
    bool found_non_phi = false;
    
    for (auto* inst : bb->getInstructions()) {
        if (dynamic_cast<PhiInst*>(inst)) {
            if (found_non_phi) {
                last_error_ = "Phi instruction after non-phi instruction in block " + bb->getName();
                return false;
            }
        } else {
            found_non_phi = true;
        }
    }
    
    return true;
}

bool IRVerifier::verifyOperands(Instruction* inst) const {
    for (size_t i = 0; i < inst->getNumOperands(); ++i) {
        if (!inst->getOperand(i)) {
            last_error_ = "Instruction has null operand at index " + std::to_string(i);
            return false;
        }
    }
    
    return true;
}

bool IRVerifier::verifyTypes(Instruction* inst) const {
    return true;
}

}

#endif