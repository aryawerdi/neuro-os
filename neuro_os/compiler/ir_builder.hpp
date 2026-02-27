#ifndef NEURO_OS_COMPILER_IR_BUILDER_HPP
#define NEURO_OS_COMPILER_IR_BUILDER_HPP

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace neuro_os::compiler {

enum class Type {
    Void,
    Int32,
    Int64,
    Float,
    Double,
    Pointer,
    Label
};

enum class OpCode {
    Alloca,
    Load,
    Store,
    Add,
    Sub,
    Mul,
    SDiv,
    SRem,
    UDiv,
    URem,
    And,
    Or,
    Xor,
    Shl,
    LShr,
    AShr,
    ICmp,
    FCmp,
    Call,
    Br,
    CondBr,
    Ret,
    RetVoid,
    Phi,
    Sext,
    Zext,
    Trunc,
    BitCast,
    GEP
};

enum class ComparePredicate {
    Eq,
    Ne,
    Ugt,
    Uge,
    Ult,
    Ule,
    Sgt,
    Sge,
    Oeq,
    One,
    Ogt,
    Oge,
    Olt,
    Ole,
    Ord,
    Uno
};

class Value {
public:
    virtual ~Value() {}
    virtual Type getType() const = 0;
    virtual std::string getName() const { return ""; }
    virtual void setName(const std::string& name) {}
};

class TypeUtils {
public:
    static std::string typeToString(Type t);
    static size_t getTypeSize(Type t);
};

class BasicBlock;
class Function;

struct Use {
    Value* value = nullptr;
    void* user = nullptr;
    size_t index = 0;
};

class User : public Value {
public:
    std::vector<Use> operands;
    
    void addOperand(Value* v) {
        Use u;
        u.value = v;
        u.user = this;
        u.index = operands.size();
        operands.push_back(u);
    }
    
    Value* getOperand(size_t idx) const {
        if (idx < operands.size()) {
            return operands[idx].value;
        }
        return nullptr;
    }
    
    size_t getNumOperands() const { return operands.size(); }
};

class Argument;

class Function : public User {
public:
    Function(Type ret_ty, const std::string& name);
    
    Type getType() const override { return Type::Label; }
    std::string getName() const override { return name_; }
    void setName(const std::string& name) override { name_ = name; }
    
    Type getReturnType() const { return returnType; }
    BasicBlock* getEntryBlock() const;
    const std::vector<BasicBlock*>& getBasicBlocks() const { return blocks; }
    const std::vector<Argument*>& getArguments() const { return arguments; }
    
    BasicBlock* CreateBasicBlock(const std::string& name = "");
    
    void addArgument(Argument* arg);
    void addBasicBlock(BasicBlock* bb);
    
    size_t getNumBasicBlocks() const { return blocks.size(); }
    size_t getNumArguments() const { return arguments.size(); }
    
    void dump() const;

private:
    Type returnType;
    std::string name_;
    std::vector<BasicBlock*> blocks;
    std::vector<Argument*> arguments;
};

class Argument : public Value {
public:
    Argument(Type ty, const std::string& name, Function* parent);
    
    Type getType() const override { return type_; }
    std::string getName() const override { return name_; }
    void setName(const std::string& name) override { name_ = name; }
    Function* getParent() const { return parent_; }
    unsigned getArgNo() const;
    
private:
    Type type_;
    std::string name_;
    Function* parent_;
};

class Instruction : public User {
public:
    BasicBlock* parent = nullptr;
    size_t id = 0;
    static size_t next_id;
    
    Instruction(OpCode op, Type ty);
    
    Type getType() const override { return type_; }
    BasicBlock* getParent() const { return parent; }
    
    virtual std::string getOpName() const = 0;
    virtual bool isTerminator() const { return false; }
    virtual bool isBinaryOp() const { return false; }
    virtual bool isMemoryOp() const { return false; }
    
    OpCode getOpCode() const { return opcode_; }

protected:
    OpCode opcode_;
    Type type_;
};

size_t Instruction::next_id = 0;

class AllocaInst : public Instruction {
public:
    explicit AllocaInst(Type elem_type);
    
    Type getElementType() const { return elementType; }
    std::string getOpName() const override { return "alloca"; }
    bool isMemoryOp() const override { return true; }
    
private:
    Type elementType;
};

class LoadInst : public Instruction {
public:
    explicit LoadInst(Value* ptr);
    
    Value* getPointer() const { return getOperand(0); }
    std::string getOpName() const override { return "load"; }
    bool isMemoryOp() const override { return true; }
};

class StoreInst : public Instruction {
public:
    StoreInst(Value* val, Value* ptr);
    
    Value* getValue() const { return getOperand(0); }
    Value* getPointer() const { return getOperand(1); }
    std::string getOpName() const override { return "store"; }
    bool isMemoryOp() const override { return true; }
};

class BinOpInst : public Instruction {
public:
    BinOpInst(OpCode op, Type ty);
    
    std::string getOpName() const override {
        auto op = getOpCode();
        if (op == OpCode::Add) return "add";
        if (op == OpCode::Sub) return "sub";
        if (op == OpCode::Mul) return "mul";
        if (op == OpCode::SDiv) return "sdiv";
        if (op == OpCode::UDiv) return "udiv";
        if (op == OpCode::SRem) return "srem";
        if (op == OpCode::URem) return "urem";
        if (op == OpCode::And) return "and";
        if (op == OpCode::Or) return "or";
        if (op == OpCode::Xor) return "xor";
        if (op == OpCode::Shl) return "shl";
        if (op == OpCode::LShr) return "lshr";
        if (op == OpCode::AShr) return "ashr";
        return "binop";
    }
    
    bool isBinaryOp() const override { return true; }
    
    Value* getLHS() const { return getOperand(0); }
    Value* getRHS() const { return getOperand(1); }
};

class ICmpInst : public Instruction {
public:
    ICmpInst(ComparePredicate pred, Value* lhs, Value* rhs);
    
    ComparePredicate getPredicate() const { return predicate; }
    std::string getOpName() const override { return "icmp"; }
    
    std::string getPredicateName() const {
        auto pred = getPredicate();
        if (pred == ComparePredicate::Eq) return "eq";
        if (pred == ComparePredicate::Ne) return "ne";
        if (pred == ComparePredicate::Ugt) return "ugt";
        if (pred == ComparePredicate::Uge) return "uge";
        if (pred == ComparePredicate::Ult) return "ult";
        if (pred == ComparePredicate::Ule) return "ule";
        if (pred == ComparePredicate::Sgt) return "sgt";
        if (pred == ComparePredicate::Sge) return "sge";
        return "unknown";
    }
    
private:
    ComparePredicate predicate;
};

class CallInst : public Instruction {
public:
    CallInst(Function* callee, std::vector<Value*> args);
    
    Function* getCallee() const { return static_cast<Function*>(getOperand(0)); }
    std::string getOpName() const override { return "call"; }
    
    size_t getNumArgs() const { return getNumOperands() > 0 ? getNumOperands() - 1 : 0; }
    Value* getArg(size_t idx) const { return getOperand(idx + 1); }
};

class BranchInst : public Instruction {
public:
    explicit BranchInst(BasicBlock* dest);
    
    BranchInst(Value* cond, BasicBlock* trueBB, BasicBlock* falseBB);
    
    bool isConditional() const { return falseDest != nullptr; }
    BasicBlock* getTrueDest() const { return trueDest; }
    BasicBlock* getFalseDest() const { return falseDest; }
    Value* getCondition() const { return condition; }
    std::string getOpName() const override { return "br"; }
    bool isTerminator() const override { return true; }
    
private:
    Value* condition = nullptr;
    BasicBlock* trueDest = nullptr;
    BasicBlock* falseDest = nullptr;
};

class ReturnInst : public Instruction {
public:
    explicit ReturnInst(Value* val);
    ReturnInst();
    
    Value* getReturnValue() const { return returnValue; }
    bool hasReturnValue() const { return returnValue != nullptr; }
    std::string getOpName() const override { return "ret"; }
    bool isTerminator() const override { return true; }
    
private:
    Value* returnValue = nullptr;
};

class SextInst : public Instruction {
public:
    SextInst(Value* val, Type destTy);
    
    Value* getOperandValue() const { return getOperand(0); }
    std::string getOpName() const override { return "sext"; }
};

class ZextInst : public Instruction {
public:
    ZextInst(Value* val, Type destTy);
    
    Value* getOperandValue() const { return getOperand(0); }
    std::string getOpName() const override { return "zext"; }
};

class BasicBlock : public Value {
public:
    explicit BasicBlock(const std::string& name = "");
    
    Type getType() const override { return Type::Label; }
    std::string getName() const override { return name_; }
    void setName(const std::string& name) override { name_ = name; }
    
    void addInstruction(Instruction* inst);
    void addFront(Instruction* inst);
    
    Instruction* getTerminator();
    
    const std::vector<Instruction*>& getInstructions() const { return instructions; }
    Function* getParent() const { return parent; }
    
    std::string getLabelName() const {
        return name_.empty() ? "bb" + std::to_string(id_) : name_;
    }
    
    size_t getId() const { return id_; }
    static size_t getNextId() { return next_id_; }
    
    void setParent(Function* f) { parent = f; }

private:
    std::string name_;
    std::vector<Instruction*> instructions;
    Function* parent = nullptr;
    size_t id_ = next_id_++;
    static size_t next_id_;
};

size_t BasicBlock::next_id_ = 0;

class Module : public Value {
public:
    explicit Module(const std::string& name = "main");
    
    Type getType() const override { return Type::Void; }
    std::string getName() const override { return name_; }
    void setName(const std::string& name) override { name_ = name; }
    
    void addFunction(Function* func);
    Function* getFunction(const std::string& name) const;
    const std::vector<Function*>& getFunctions() const { return functions; }
    size_t getNumFunctions() const { return functions.size(); }
    
    void dump() const;

private:
    std::string name_;
    std::vector<Function*> functions;
};

Function::Function(Type ret_ty, const std::string& name)
    : returnType(ret_ty), name_(name) {}

BasicBlock* Function::getEntryBlock() const {
    return blocks.empty() ? nullptr : blocks.front();
}

void Function::addArgument(Argument* arg) {
    arguments.push_back(arg);
}

void Function::addBasicBlock(BasicBlock* bb) {
    bb->setParent(this);
    blocks.push_back(bb);
}

BasicBlock* Function::CreateBasicBlock(const std::string& name) {
    auto* bb = new BasicBlock(name);
    addBasicBlock(bb);
    return bb;
}

Argument::Argument(Type ty, const std::string& name, Function* parent)
    : type_(ty), name_(name), parent_(parent) {}

unsigned Argument::getArgNo() const {
    auto& args = parent_->getArguments();
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == this) return static_cast<unsigned>(i);
    }
    return 0;
}

Instruction::Instruction(OpCode op, Type ty) : opcode_(op), type_(ty) {
    id = next_id++;
}

AllocaInst::AllocaInst(Type elem_type)
    : Instruction(OpCode::Alloca, Type::Pointer), elementType(elem_type) {}

LoadInst::LoadInst(Value* ptr)
    : Instruction(OpCode::Load, Type::Int32) {
    addOperand(ptr);
}

StoreInst::StoreInst(Value* val, Value* ptr)
    : Instruction(OpCode::Store, Type::Void) {
    addOperand(val);
    addOperand(ptr);
}

BinOpInst::BinOpInst(OpCode op, Type ty)
    : Instruction(op, ty) {}

ICmpInst::ICmpInst(ComparePredicate pred, Value* lhs, Value* rhs)
    : Instruction(OpCode::ICmp, Type::Int32), predicate(pred) {
    addOperand(lhs);
    addOperand(rhs);
}

CallInst::CallInst(Function* callee, std::vector<Value*> args)
    : Instruction(OpCode::Call, callee->getReturnType()) {
    addOperand(callee);
    for (auto* arg : args) {
        addOperand(arg);
    }
}

BranchInst::BranchInst(BasicBlock* dest)
    : Instruction(OpCode::Br, Type::Void), trueDest(dest) {}

BranchInst::BranchInst(Value* cond, BasicBlock* trueBB, BasicBlock* falseBB)
    : Instruction(OpCode::CondBr, Type::Void),
      condition(cond), trueDest(trueBB), falseDest(falseBB) {
    addOperand(cond);
}

ReturnInst::ReturnInst(Value* val)
    : Instruction(OpCode::Ret, Type::Void), returnValue(val) {
    if (val) addOperand(val);
}

ReturnInst::ReturnInst() : Instruction(OpCode::RetVoid, Type::Void) {}

SextInst::SextInst(Value* val, Type destTy)
    : Instruction(OpCode::Sext, destTy) {
    addOperand(val);
}

ZextInst::ZextInst(Value* val, Type destTy)
    : Instruction(OpCode::Zext, destTy) {
    addOperand(val);
}

BasicBlock::BasicBlock(const std::string& name) : name_(name) {
    setName(name);
}

void BasicBlock::addInstruction(Instruction* inst) {
    inst->parent = this;
    instructions.push_back(inst);
}

void BasicBlock::addFront(Instruction* inst) {
    inst->parent = this;
    instructions.insert(instructions.begin(), inst);
}

Instruction* BasicBlock::getTerminator() {
    if (!instructions.empty()) {
        Instruction* last = instructions.back();
        if (last->isTerminator()) {
            return last;
        }
    }
    return nullptr;
}

Module::Module(const std::string& name) : name_(name) {}

void Module::addFunction(Function* func) {
    functions.push_back(func);
}

Function* Module::getFunction(const std::string& name) const {
    for (auto* f : functions) {
        if (f->getName() == name) return f;
    }
    return nullptr;
}

class IRBuilder {
public:
    explicit IRBuilder(Module* mod = nullptr) : module_(mod), current_block_(nullptr) {}
    
    void setModule(Module* mod) { module_ = mod; }
    void setInsertPoint(BasicBlock* bb) { current_block_ = bb; }
    BasicBlock* getInsertBlock() const { return current_block_; }
    
    AllocaInst* CreateAlloca(Type elemType, const std::string& name = "") {
        auto* alloca = new AllocaInst(elemType);
        alloca->setName(name);
        if (current_block_) {
            current_block_->addInstruction(alloca);
        }
        return alloca;
    }
    
    LoadInst* CreateLoad(Type ty, Value* ptr, const std::string& name = "") {
        auto* load = new LoadInst(ptr);
        load->setName(name);
        if (current_block_) {
            current_block_->addInstruction(load);
        }
        return load;
    }
    
    StoreInst* CreateStore(Value* val, Value* ptr) {
        auto* store = new StoreInst(val, ptr);
        if (current_block_) {
            current_block_->addInstruction(store);
        }
        return store;
    }
    
    BinOpInst* CreateAdd(Value* lhs, Value* rhs, const std::string& name = "") {
        return CreateBinOp(OpCode::Add, lhs, rhs, name);
    }
    
    BinOpInst* CreateSub(Value* lhs, Value* rhs, const std::string& name = "") {
        return CreateBinOp(OpCode::Sub, lhs, rhs, name);
    }
    
    BinOpInst* CreateMul(Value* lhs, Value* rhs, const std::string& name = "") {
        return CreateBinOp(OpCode::Mul, lhs, rhs, name);
    }
    
    BinOpInst* CreateSDiv(Value* lhs, Value* rhs, const std::string& name = "") {
        return CreateBinOp(OpCode::SDiv, lhs, rhs, name);
    }
    
    BinOpInst* CreateAnd(Value* lhs, Value* rhs, const std::string& name = "") {
        return CreateBinOp(OpCode::And, lhs, rhs, name);
    }
    
    BinOpInst* CreateOr(Value* lhs, Value* rhs, const std::string& name = "") {
        return CreateBinOp(OpCode::Or, lhs, rhs, name);
    }
    
    BinOpInst* CreateXor(Value* lhs, Value* rhs, const std::string& name = "") {
        return CreateBinOp(OpCode::Xor, lhs, rhs, name);
    }
    
    BinOpInst* CreateBinOp(OpCode op, Value* lhs, Value* rhs, const std::string& name = "") {
        auto* binop = new BinOpInst(op, Type::Int32);
        binop->setName(name);
        binop->addOperand(lhs);
        binop->addOperand(rhs);
        if (current_block_) {
            current_block_->addInstruction(binop);
        }
        return binop;
    }
    
    ICmpInst* CreateICmp(ComparePredicate pred, Value* lhs, Value* rhs, const std::string& name = "") {
        auto* icmp = new ICmpInst(pred, lhs, rhs);
        icmp->setName(name);
        if (current_block_) {
            current_block_->addInstruction(icmp);
        }
        return icmp;
    }
    
    CallInst* CreateCall(Function* callee, std::vector<Value*> args, const std::string& name = "") {
        auto* call = new CallInst(callee, args);
        call->setName(name);
        if (current_block_) {
            current_block_->addInstruction(call);
        }
        return call;
    }
    
    BranchInst* CreateBr(BasicBlock* dest) {
        auto* br = new BranchInst(dest);
        if (current_block_) {
            current_block_->addInstruction(br);
        }
        return br;
    }
    
    BranchInst* CreateCondBr(Value* cond, BasicBlock* trueBB, BasicBlock* falseBB) {
        auto* br = new BranchInst(cond, trueBB, falseBB);
        if (current_block_) {
            current_block_->addInstruction(br);
        }
        return br;
    }
    
    ReturnInst* CreateRet(Value* val) {
        auto* ret = new ReturnInst(val);
        if (current_block_) {
            current_block_->addInstruction(ret);
        }
        return ret;
    }
    
    ReturnInst* CreateRetVoid() {
        auto* ret = new ReturnInst();
        if (current_block_) {
            current_block_->addInstruction(ret);
        }
        return ret;
    }
    
    SextInst* CreateSExt(Value* val, Type destTy, const std::string& name = "") {
        auto* sext = new SextInst(val, destTy);
        sext->setName(name);
        if (current_block_) {
            current_block_->addInstruction(sext);
        }
        return sext;
    }
    
    ZextInst* CreateZExt(Value* val, Type destTy, const std::string& name = "") {
        auto* zext = new ZextInst(val, destTy);
        zext->setName(name);
        if (current_block_) {
            current_block_->addInstruction(zext);
        }
        return zext;
    }
    
    Function* CreateFunction(Type retTy, const std::string& name) {
        auto* func = new Function(retTy, name);
        if (module_) {
            module_->addFunction(func);
        }
        return func;
    }
    
    BasicBlock* CreateBasicBlock(const std::string& name = "", Function* parent = nullptr) {
        auto* bb = new BasicBlock(name);
        if (parent) {
            parent->addBasicBlock(bb);
        }
        return bb;
    }

private:
    Module* module_ = nullptr;
    BasicBlock* current_block_ = nullptr;
};

}

#endif
