#include <cassert>
#include <exception>
#include <iostream>

#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>

#include "Kaleidoscope.h"
#include "KaleidoscopeParser.h"

llvm::Value *Constant::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  return llvm::ConstantFP::get(Builder.getContext(), llvm::APFloat(Value));
}

llvm::Value *Variable::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  return Symtab.lookup(Name);
}

llvm::Value *UnaryExpr::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  return nullptr;
}

llvm::Value *BinaryExpr::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  llvm::Value *LHSValue = LHS->codegen(Module, Builder, Symtab);
  llvm::Value *RHSValue = RHS->codegen(Module, Builder, Symtab);

  switch (Opcode) {
    case '+':
      return Builder.CreateFAdd(LHSValue, RHSValue, "addtmp");
    case '-':
      return Builder.CreateFSub(LHSValue, RHSValue, "subtmp");
    case '*':
      return Builder.CreateFMul(LHSValue, RHSValue, "multmp");
    case '<': {
      llvm::Value *CmpValue =
        Builder.CreateFCmpULT(LHSValue, RHSValue, "cmptmp");
      return Builder.CreateUIToFP(CmpValue, Builder.getDoubleTy(), "booltmp");
    }
    default:
      assert(0 && "Invalid binary operator");
  }
}

llvm::Value *CallExpr::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  llvm::Function *Callee = Module.getFunction(Name);
  if (!Callee) {
    throw std::runtime_error("Unknown function referenced: " + Name);
  }

  if (Callee->arg_size() != Args.size()) {
    throw std::runtime_error("Incorrect # arguments passed");
  }

  std::vector<llvm::Value *> ArgValues;
  for (auto &Arg : Args) {
    ArgValues.push_back(Arg->codegen(Module, Builder, Symtab));
  }

  return Builder.CreateCall(Callee, ArgValues, "calltmp");
}

llvm::Value *IfExpr::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  return nullptr;
}

llvm::Function *Prototype::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  std::vector<llvm::Type *> Doubles(Params.size(), Builder.getDoubleTy());

  llvm::FunctionType *FunctionType =
    llvm::FunctionType::get(Builder.getDoubleTy(), Doubles, false);

  llvm::Function *Function = llvm::Function::Create(
    FunctionType, llvm::Function::ExternalLinkage, Name, Module);

  size_t Idx = 0;
  for (auto &Param : Function->args()) {
    Param.setName(Params[Idx]);
    Symtab.define(Params[Idx], &Param);
    Idx++;
  }

  return Function;
}

llvm::Function *Function::codegen(
  llvm::Module &Module,
  llvm::IRBuilder<> &Builder,
  llvm::legacy::FunctionPassManager &PassManager,
  SymbolTable &Symtab) const {

  Symtab.enterScope();

  llvm::Function *Func = Proto->codegen(Module, Builder, Symtab);

  /* TODO: Remove function if error occurs */

  if (!Func) {
    Symtab.leaveScope();
    throw std::runtime_error(
      "Function cannot be redefined: " + Proto->getName());
  }

  llvm::BasicBlock *BB =
    llvm::BasicBlock::Create(Builder.getContext(), "Entry", Func);
  Builder.SetInsertPoint(BB);

  llvm::Value *RetValue = Body->codegen(Module, Builder, Symtab);
  Builder.CreateRet(RetValue);

  Symtab.leaveScope();

  llvm::verifyFunction(*Func);

  PassManager.run(*Func);

  return Func;
}

int main(int argc, char *argv[]) {
  llvm::LLVMContext Context;
  llvm::Module Module("cool jit", Context);
  llvm::IRBuilder<> Builder(Context);

  llvm::legacy::FunctionPassManager PassManager(&Module);

  // Do simple "peephole" optimizations and bit-twiddling optzns
  PassManager.add(llvm::createInstructionCombiningPass());
  // Reassociate expressions
  PassManager.add(llvm::createReassociatePass());
  // Eliminate common subexpressions
  PassManager.add(llvm::createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc)
  PassManager.add(llvm::createCFGSimplificationPass());

  PassManager.doInitialization();

  SymbolTable Symtab;

  yy::parser parse(Module, Builder, PassManager, Symtab);

  std::cout << ">>> " << std::flush;
  do {
    parse();
  } while (std::cin);
  std::cout << std::endl;

  return 0;
}