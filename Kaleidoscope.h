#ifndef KALEIDOSCOPE_H
#define KALEIDOSCOPE_H

#include <memory>
#include <string>
#include <vector>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include "SymbolTable.h"

class Expr {
public:
  virtual ~Expr(void) = default;

  virtual llvm::Value *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const = 0;
};

class Constant : public Expr {
private:
  double Value;

public:
  Constant(double Value) : Value(Value) {}

  llvm::Value *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const override;
};

class Variable : public Expr {
private:
  std::string Name;

public:
  Variable(const std::string &Name) : Name(Name) {}

  const std::string &getName(void) const {
    return Name;
  }

  llvm::Value *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const override;
};

class UnaryExpr : public Expr {
private:
  char Operator;
  std::unique_ptr<Expr> Operand;

public:
  UnaryExpr(char Operator, std::unique_ptr<Expr> &&Operand) :
    Operator(Operator), Operand(std::move(Operand)) {}

  llvm::Value *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const override;
};

class BinaryExpr : public Expr {
private:
  char Operator;
  std::unique_ptr<Expr> LHS, RHS;

public:
  BinaryExpr(
    char Operator, std::unique_ptr<Expr> &&LHS, std::unique_ptr<Expr> &&RHS) :
    Operator(Operator), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

  llvm::Value *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const override;
};

class CallExpr : public Expr {
private:
  std::string Name;
  std::vector<std::unique_ptr<Expr>> Args;

public:
  CallExpr(const std::string &Name, std::vector<std::unique_ptr<Expr>> &&Args) :
    Name(Name), Args(std::move(Args)) {}

  llvm::Value *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const override;
};

class IfExpr : public Expr {
private:
  std::unique_ptr<Expr> Cond, Then, Else;

public:
  IfExpr(
    std::unique_ptr<Expr> &&Cond,
    std::unique_ptr<Expr> &&Then,
    std::unique_ptr<Expr> &&Else) :
    Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

  llvm::Value *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const override;
};

class ForExpr : public Expr {
private:
  std::string VarName;
  std::unique_ptr<Expr> Init, Cond, Step, Body;

public:
  ForExpr(
    const std::string &VarName,
    std::unique_ptr<Expr> &&Init,
    std::unique_ptr<Expr> &&Cond,
    std::unique_ptr<Expr> &&Step,
    std::unique_ptr<Expr> &&Body) :
    VarName(VarName),
    Init(std::move(Init)),
    Cond(std::move(Cond)),
    Step(std::move(Step)),
    Body(std::move(Body)) {}

  llvm::Value *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const override;
};

class VarExpr : public Expr {
private:
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> Defs;
  std::unique_ptr<Expr> Body;

public:
  VarExpr(
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> &&Defs,
    std::unique_ptr<Expr> &&Body) :
    Defs(std::move(Defs)), Body(std::move(Body)) {}

  llvm::Value *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const override;
};

class Prototype final {
private:
  std::string Name;
  std::vector<std::string> Params;

public:
  Prototype(const std::string &Name, std::vector<std::string> &&Params) :
    Name(Name), Params(std::move(Params)) {}

  Prototype(char Operator, const std::string &Operand) :
    Name(std::string("unary") + Operator), Params{Operand} {}

  Prototype(char Operator, const std::string &LHS, const std::string &RHS) :
    Name(std::string("binary") + Operator), Params{LHS, RHS} {}

  llvm::Function *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    SymbolTable &Symtab) const;

  const std::string &getName(void) const {
    return Name;
  }
};

class Function final {
private:
  std::unique_ptr<Prototype> Proto;
  std::unique_ptr<Expr> Body;

public:
  Function(std::unique_ptr<Prototype> &&Proto, std::unique_ptr<Expr> &&Body) :
    Proto(std::move(Proto)), Body(std::move(Body)) {}

  llvm::Function *codegen(
    llvm::Module &Module,
    llvm::IRBuilder<> &Builder,
    llvm::legacy::FunctionPassManager &PassManager,
    SymbolTable &Symtab) const;
};

#endif