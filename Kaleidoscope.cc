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
  llvm::Value *OperandValue = Operand->codegen(Module, Builder, Symtab);
  llvm::Function *Func = Module.getFunction(std::string("unary") + Op);
  if (!Func) {
    throw std::runtime_error(std::string("Unknown unary operator: ") + Op);
  }
  return Builder.CreateCall(Func, OperandValue, "unop");
}

llvm::Value *BinaryExpr::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  llvm::Value *LHSValue = LHS->codegen(Module, Builder, Symtab);
  llvm::Value *RHSValue = RHS->codegen(Module, Builder, Symtab);
  switch (Op) {
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
    default: {
      // If it wasn't a buildin binary operator, it must be a user defined one
      llvm::Function *Func = Module.getFunction(std::string("binary") + Op);
      assert(Func && "Invalid binary operator");
      llvm::Value *Operands[2] = {LHSValue, RHSValue};
      return Builder.CreateCall(Func, Operands, "binop");
    }
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
  llvm::LLVMContext &Context = Builder.getContext();

  // Convert condition to a bool by comparing non-equal to 0.0
  llvm::Value *CondValue = Builder.CreateFCmpONE(
    Cond->codegen(Module, Builder, Symtab),
    llvm::ConstantFP::get(Context, llvm::APFloat(0.0)), "cond");

  llvm::Function *Func = Builder.GetInsertBlock()->getParent();

  llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(Context, "Then", Func);
  // Insert into the function later
  llvm::BasicBlock *ElseBB  = llvm::BasicBlock::Create(Context, "Else");
  llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(Context, "Merge");

  Builder.CreateCondBr(CondValue, ThenBB, ElseBB);

  // Emit `then` block
  Builder.SetInsertPoint(ThenBB);
  llvm::Value *ThenValue = Then->codegen(Module, Builder, Symtab);
  Builder.CreateBr(MergeBB);
  // Note: codegen of 'Then' can change the current block
  ThenBB = Builder.GetInsertBlock();

  // Emit 'else' block
  Func->getBasicBlockList().push_back(ElseBB);
  Builder.SetInsertPoint(ElseBB);
  llvm::Value *ElseValue = Else->codegen(Module, Builder, Symtab);
  Builder.CreateBr(MergeBB);
  // Note: codegen of 'Else' can change the current block
  ElseBB = Builder.GetInsertBlock();

  // Emit 'merge' block
  Func->getBasicBlockList().push_back(MergeBB);
  Builder.SetInsertPoint(MergeBB);
  llvm::PHINode *Phi = Builder.CreatePHI(Builder.getDoubleTy(), 2, "iftmp");
  Phi->addIncoming(ThenValue, ThenBB);
  Phi->addIncoming(ElseValue, ElseBB);

  return Phi;
}

llvm::Value *ForExpr::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  // Emit 'init' code first, without 'variable' in scope
  llvm::Value *InitValue = Init->codegen(Module, Builder, Symtab);

  llvm::BasicBlock *HeadBB = Builder.GetInsertBlock();
  llvm::BasicBlock *LoopBB = llvm::BasicBlock::Create(
    Builder.getContext(), "Loop", Builder.GetInsertBlock()->getParent());

  // Insert an explicit fall through from the current block to the LoopBB
  Builder.CreateBr(LoopBB);

  Symtab.enterScope();

  // Emit `loop` block
  Builder.SetInsertPoint(LoopBB);
  llvm::PHINode *Phi =
    Builder.CreatePHI(Builder.getDoubleTy(), 2, VarName.c_str());
  Phi->addIncoming(InitValue, HeadBB);
  Symtab.define(VarName, Phi);
  Body->codegen(Module, Builder, Symtab);
  llvm::Value *StepValue = nullptr;
  if (Step) {
    StepValue = Step->codegen(Module, Builder, Symtab);
  } else {
    StepValue = llvm::ConstantFP::get(Builder.getContext(), llvm::APFloat(1.0));
  }
  llvm::Value *NextValue = Builder.CreateFAdd(Phi, StepValue, "nextvar");
  // Compute the loop condition
  llvm::Value *CondValue = Builder.CreateFCmpONE(
    Cond->codegen(Module, Builder, Symtab),
    llvm::ConstantFP::get(Builder.getContext(), llvm::APFloat(0.0)), "cond");

  // Note: current block can be changed
  LoopBB                   = Builder.GetInsertBlock();
  llvm::BasicBlock *ExitBB = llvm::BasicBlock::Create(
    Builder.getContext(), "Exit", Builder.GetInsertBlock()->getParent());

  // Insert the conditional branch into the end of LoopBB
  Builder.CreateCondBr(CondValue, LoopBB, ExitBB);

  // Add a new entry to the Phi node for the backedge
  Phi->addIncoming(NextValue, LoopBB);

  Symtab.leaveScope();

  // Any new code will be inserted in ExitBB
  Builder.SetInsertPoint(ExitBB);

  // For expr always returns 0.0
  return llvm::Constant::getNullValue(Builder.getDoubleTy());
}

llvm::Function *Prototype::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  std::vector<llvm::Type *> Doubles(Params.size(), Builder.getDoubleTy());

  llvm::FunctionType *FuncType =
    llvm::FunctionType::get(Builder.getDoubleTy(), Doubles, false);

  llvm::Function *Func = llvm::Function::Create(
    FuncType, llvm::Function::ExternalLinkage, Name, Module);

  size_t Idx = 0;
  for (auto &Param : Func->args()) {
    Param.setName(Params[Idx]);
    Symtab.define(Params[Idx], &Param);
    Idx++;
  }

  return Func;
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