#ifndef NEURO_OS_COMPILER_IR_PRINTER_HPP
#define NEURO_OS_COMPILER_IR_PRINTER_HPP

#include "ir_builder.hpp"
#include <iostream>
#include <sstream>
#include <string>

namespace neuro_os::compiler {

class IRPrinter {
public:
    explicit IRPrinter(std::ostream& os = std::cout) : os_(os), indent_(0) {}
    
    void printModule(const Module* mod);
    void printFunction(const Function* func);
    void printBasicBlock(const BasicBlock* bb);
    void printInstruction(const Instruction* inst);
    
    void setIndent(size_t indent) { indent_ = indent; }
    void increaseIndent() { indent_ += 2; }
    void decreaseIndent() { if (indent_ >= 2) indent_ -= 2; }
    
private:
    std::ostream& os_;
    size_t indent_;
    
    void printIndent() {
        for (size_t i = 0; i < indent_; ++i) {
            os_ << ' ';
        }
    }
    
    std::string typeToString(Type ty) const;
    std::string valueToString(const Value* val) const;
};

std::string IRPrinter::typeToString(Type ty) const {
    if (ty == Type::Void) return "void";
    if (ty == Type::Int32) return "i32";
    if (ty == Type::Int64) return "i64";
    if (ty == Type::Float) return "float";
    if (ty == Type::Double) return "double";
    if (ty == Type::Pointer) return "ptr";
    if (ty == Type::Label) return "label";
    return "unknown";
}

std::string IRPrinter::valueToString(const Value* val) const {
    if (!val) return "null";
    
    if (auto* arg = dynamic_cast<const Argument*>(val)) {
        return "%" + arg->getName();
    }
    
    if (auto* bb = dynamic_cast<const BasicBlock*>(val)) {
        return bb->getLabelName();
    }
    
    if (auto* inst = dynamic_cast<const Instruction*>(val)) {
        if (!inst->getName().empty()) {
            return "%" + inst->getName();
        }
        return "%" + std::to_string(inst->id);
    }
    
    return "unknown";
}

void IRPrinter::printModule(const Module* mod) {
    os_ << "; Module: " << mod->getName() << "\n";
    os_ << "\n";
    
    for (const auto* func : mod->getFunctions()) {
        printFunction(func);
        os_ << "\n";
    }
}

void IRPrinter::printFunction(const Function* func) {
    os_ << "define " << typeToString(func->getReturnType()) << " @" << func->getName() << "(";
    
    const auto& args = func->getArguments();
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) os_ << ", ";
        os_ << typeToString(args[i]->getType()) << " %" << args[i]->getName();
    }
    os_ << ") {\n";
    
    increaseIndent();
    for (const auto* bb : func->getBasicBlocks()) {
        printBasicBlock(bb);
    }
    decreaseIndent();
    os_ << "}\n";
}

void IRPrinter::printBasicBlock(const BasicBlock* bb) {
    os_ << bb->getLabelName() << ":\n";
    
    increaseIndent();
    for (const auto* inst : bb->getInstructions()) {
        printInstruction(inst);
    }
    decreaseIndent();
}

void IRPrinter::printInstruction(const Instruction* inst) {
    printIndent();
    
    auto* result = const_cast<Instruction*>(inst);
    if (inst->getType() != Type::Void) {
        os_ << "%" << inst->getName() << " = ";
    }
    
    os_ << inst->getOpName() << " ";
    
    if (auto* alloca = dynamic_cast<const AllocaInst*>(inst)) {
        os_ << typeToString(alloca->getElementType());
    }
    else if (auto* binop = dynamic_cast<const BinOpInst*>(inst)) {
        os_ << typeToString(binop->getType()) << " ";
        os_ << valueToString(binop->getLHS()) << ", ";
        os_ << valueToString(binop->getRHS());
    }
    else if (auto* icmp = dynamic_cast<const ICmpInst*>(inst)) {
        os_ << icmp->getPredicateName() << " ";
        os_ << typeToString(icmp->getOperand(0)->getType()) << " ";
        os_ << valueToString(icmp->getOperand(0)) << ", ";
        os_ << valueToString(icmp->getOperand(1));
    }
    else if (auto* load = dynamic_cast<const LoadInst*>(inst)) {
        os_ << typeToString(load->getType()) << " ";
        os_ << valueToString(load->getPointer());
    }
    else if (auto* store = dynamic_cast<const StoreInst*>(inst)) {
        os_ << typeToString(store->getValue()->getType()) << " ";
        os_ << valueToString(store->getValue()) << ", ";
        os_ << valueToString(store->getPointer());
    }
    else if (auto* br = dynamic_cast<const BranchInst*>(inst)) {
        if (br->isConditional()) {
            os_ << valueToString(br->getCondition()) << ", ";
            os_ << br->getTrueDest()->getLabelName() << ", ";
            os_ << br->getFalseDest()->getLabelName();
        } else {
            os_ << br->getTrueDest()->getLabelName();
        }
    }
    else if (auto* ret = dynamic_cast<const ReturnInst*>(inst)) {
        if (ret->hasReturnValue()) {
            os_ << typeToString(ret->getReturnValue()->getType()) << " ";
            os_ << valueToString(ret->getReturnValue());
        }
    }
    else if (auto* call = dynamic_cast<const CallInst*>(inst)) {
        os_ << typeToString(call->getType()) << " ";
        os_ << "@" << call->getCallee()->getName() << "(";
        for (size_t i = 0; i < call->getNumArgs(); ++i) {
            if (i > 0) os_ << ", ";
            os_ << valueToString(call->getArg(i));
        }
        os_ << ")";
    }
    else if (auto* sext = dynamic_cast<const SextInst*>(inst)) {
        os_ << typeToString(sext->getOperandValue()->getType()) << " ";
        os_ << valueToString(sext->getOperandValue()) << " to ";
        os_ << typeToString(sext->getType());
    }
    else if (auto* zext = dynamic_cast<const ZextInst*>(inst)) {
        os_ << typeToString(zext->getOperandValue()->getType()) << " ";
        os_ << valueToString(zext->getOperandValue()) << " to ";
        os_ << typeToString(zext->getType());
    }
    
    os_ << "\n";
}

void Function::dump() const {
    IRPrinter printer;
    printer.printFunction(this);
}

void Module::dump() const {
    IRPrinter printer;
    printer.printModule(this);
}

}

#endif
