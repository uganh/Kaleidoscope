#include <cassert>
#include <exception>
#include <iostream>

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/Optional.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Utils.h>

#include "Kaleidoscope.h"
#include "KaleidoscopeParser.h"

static llvm::AllocaInst *
CreateEntryAlloca(llvm::Function *Func, const std::string &Name) {
  llvm::BasicBlock &EntryBB = Func->getEntryBlock();
  llvm::IRBuilder<> Builder(&EntryBB, EntryBB.begin());
  return Builder.CreateAlloca(
    llvm::Type::getDoubleTy(Func->getContext()), nullptr, Name);
}

llvm::Value *Constant::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  return llvm::ConstantFP::get(Builder.getContext(), llvm::APFloat(Value));
}

llvm::Value *Variable::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  llvm::Value *Alloca = Symtab.lookup(Name);
  if (!Alloca) {
    throw std::runtime_error("Unknown variable: " + Name);
  }
  return Builder.CreateLoad(Alloca, Name);
}

llvm::Value *UnaryExpr::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  llvm::Value *OperandValue = Operand->codegen(Module, Builder, Symtab);
  llvm::Function *Func = Module.getFunction(std::string("unary") + Operator);
  if (!Func) {
    throw std::runtime_error(
      std::string("Unknown unary operator: ") + Operator);
  }
  return Builder.CreateCall(Func, OperandValue, "unop");
}

llvm::Value *BinaryExpr::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  llvm::Value *RHSValue = RHS->codegen(Module, Builder, Symtab);

  // Special case '=' because we don't want to emit the LHS as an expression
  if (Operator == '=') {
    // Assignment requires the LHS to be an identifier
    Variable *LHS = static_cast<Variable *>(this->LHS.get());
    assert(LHS && "Destination of '=' must be a variable");

    llvm::Value *Alloca = Symtab.lookup(LHS->getName());
    if (!Alloca) {
      throw std::runtime_error("Unknown variable: " + LHS->getName());
    }
    Builder.CreateStore(RHSValue, Alloca);
    return RHSValue;
  }

  llvm::Value *LHSValue = LHS->codegen(Module, Builder, Symtab);
  switch (Operator) {
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
      llvm::Function *Func =
        Module.getFunction(std::string("binary") + Operator);
      if (!Func) {
        throw std::runtime_error(
          std::string("Unknown binary operator: ") + Operator);
      }
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
  llvm::Function *Func = Builder.GetInsertBlock()->getParent();

  Symtab.enterScope();

  // Create an alloca for the variable in the entry block
  llvm::AllocaInst *Alloca = CreateEntryAlloca(Func, VarName);
  Symtab.define(VarName, Alloca);

  // Emit 'init' code first, without 'variable' in scope
  llvm::Value *InitValue = Init->codegen(Module, Builder, Symtab);

  // Store the value
  Builder.CreateStore(InitValue, Alloca);

  llvm::BasicBlock *HeadBB = Builder.GetInsertBlock();
  llvm::BasicBlock *LoopBB =
    llvm::BasicBlock::Create(Builder.getContext(), "Loop", Func);

  // Insert an explicit fall through from the current block to the LoopBB
  Builder.CreateBr(LoopBB);

  // Emit `loop` block
  Builder.SetInsertPoint(LoopBB);
  Body->codegen(Module, Builder, Symtab);
  llvm::Value *StepValue = nullptr;
  if (Step) {
    StepValue = Step->codegen(Module, Builder, Symtab);
  } else {
    StepValue = llvm::ConstantFP::get(Builder.getContext(), llvm::APFloat(1.0));
  }
  llvm::Value *CurrValue = Builder.CreateLoad(Alloca, VarName);
  llvm::Value *NextValue = Builder.CreateFAdd(CurrValue, StepValue, "nextvar");
  Builder.CreateStore(NextValue, Alloca);
  // Compute the loop condition
  llvm::Value *CondValue = Builder.CreateFCmpONE(
    Cond->codegen(Module, Builder, Symtab),
    llvm::ConstantFP::get(Builder.getContext(), llvm::APFloat(0.0)), "cond");

  // Note: current block can be changed
  LoopBB = Builder.GetInsertBlock();
  llvm::BasicBlock *ExitBB =
    llvm::BasicBlock::Create(Builder.getContext(), "Exit", Func);

  // Insert the conditional branch into the end of LoopBB
  Builder.CreateCondBr(CondValue, LoopBB, ExitBB);

  Symtab.leaveScope();

  // Any new code will be inserted in ExitBB
  Builder.SetInsertPoint(ExitBB);

  // For expr always returns 0.0
  return llvm::Constant::getNullValue(Builder.getDoubleTy());
}

llvm::Value *VarExpr::codegen(
  llvm::Module &Module, llvm::IRBuilder<> &Builder, SymbolTable &Symtab) const {
  llvm::Function *Func = Builder.GetInsertBlock()->getParent();

  Symtab.enterScope();

  // Register all variables and emit the initializer
  for (auto &Def : Defs) {
    const std::string &VarName        = Def.first;
    const std::unique_ptr<Expr> &Init = Def.second;

    // Emit the initializer before adding the variable to scope
    llvm::Value *InitValue = nullptr;
    if (Init) {
      InitValue = Init->codegen(Module, Builder, Symtab);
    } else {
      InitValue =
        llvm::ConstantFP::get(Builder.getContext(), llvm::APFloat(0.0));
    }

    llvm::AllocaInst *Alloca = CreateEntryAlloca(Func, VarName);
    Builder.CreateStore(InitValue, Alloca);
    Symtab.define(VarName, Alloca);
  }

  // Codegen the body, now that all variables are in scope
  llvm::Value *BodyValue = Body->codegen(Module, Builder, Symtab);

  Symtab.leaveScope();

  return BodyValue;
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
    Param.setName(Params[Idx++]);
  }

  return Func;
}

llvm::Function *Function::codegen(
  llvm::Module &Module,
  llvm::IRBuilder<> &Builder,
  llvm::legacy::FunctionPassManager &PassManager,
  SymbolTable &Symtab) const {
  llvm::Function *Func = Proto->codegen(Module, Builder, Symtab);

  /* TODO: Remove function if error occurs */

  if (!Func) {
    Symtab.leaveScope();
    throw std::runtime_error(
      "Function cannot be redefined: " + Proto->getName());
  }

  llvm::BasicBlock *EntryBB =
    llvm::BasicBlock::Create(Builder.getContext(), "Entry", Func);
  Builder.SetInsertPoint(EntryBB);

  Symtab.enterScope();

  for (auto &Param : Func->args()) {
    llvm::AllocaInst *Alloca = CreateEntryAlloca(Func, Param.getName());
    Builder.CreateStore(&Param, Alloca);
    Symtab.define(Param.getName(), Alloca);
  }

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
  // Promote allocas to registers
  PassManager.add(llvm::createPromoteMemoryToRegisterPass());
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

  // Interactive parsing and compiling
  std::cout << ">>> " << std::flush;
  yy::parser parser(Module, Builder, PassManager, Symtab);
  do {
    parser.parse();
  } while (std::cin);
  std::cout << std::endl;

  // Initialize the target registry, etc.
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetAsmPrinter();

  std::string Error;
  auto TargetTriple = llvm::sys::getDefaultTargetTriple();

  auto Target = llvm::TargetRegistry::lookupTarget(TargetTriple, Error);
  // Print an error and exit if we couldn't find the requested target
  if (!Target) {
    llvm::errs() << Error;
    exit(EXIT_FAILURE);
  }

  llvm::TargetOptions Opt;
  auto RelocModel = llvm::Optional<llvm::Reloc::Model>();
  auto TargetMachine =
    Target->createTargetMachine(TargetTriple, "generic", "", Opt, RelocModel);

  // Optimizations benefit from knowing about the target and data layout
  Module.setTargetTriple(TargetTriple);
  Module.setDataLayout(TargetMachine->createDataLayout());

  // Open the output file
  std::error_code Status;
  llvm::raw_fd_ostream Output("output.o", Status, llvm::sys::fs::OF_None);
  if (Status) {
    llvm::errs() << "Could not open file: " << Status.message();
    exit(EXIT_FAILURE);
  }

  // Define a pass that emits object code and run it
  llvm::legacy::PassManager Manager;
  if (TargetMachine->addPassesToEmitFile(
        Manager, Output, nullptr, llvm::CGFT_ObjectFile)) {
    llvm::errs() << "Target machine can't emit a file of this type";
    exit(EXIT_FAILURE);
  }
  Manager.run(Module);
  Output.flush();

  return 0;
}