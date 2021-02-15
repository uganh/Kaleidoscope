#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <llvm/IR/Value.h>

class SymbolTable {
  struct SymbolInfo {
    std::string Name;
    llvm::Value *Value;
    size_t Depth;
    size_t Outer;
  };

private:
  size_t Depth;
  std::vector<SymbolInfo> Values;
  std::unordered_map<std::string, size_t> NamedValues;

public:
  SymbolTable(void) : Depth(0) {}

  SymbolTable(const SymbolTable &) = delete;
  SymbolTable &operator=(const SymbolTable &) = delete;

  void enterScope(void) {
    Depth++;
  }

  void leaveScope(void) {
    while (!Values.empty()) {
      const auto &Info = Values.back();
      if (Info.Depth != Depth) {
        break;
      }

      if (Info.Outer != -1) {
        NamedValues[Info.Name] = Info.Outer;
      } else {
        NamedValues.erase(Info.Name);
      }

      Values.pop_back();
    }
    Depth--;
  }

  void define(const std::string &Name, llvm::Value *Value) {
    size_t Outer = -1;

    auto Iter = NamedValues.find(Name);
    if (Iter != NamedValues.cend()) {
      Outer = Iter->second;
    }

    NamedValues[Name] = Values.size();
    Values.push_back({Name, Value, Depth, Outer});
  }

  llvm::Value *lookup(const std::string &Name) const {
    auto Iter = NamedValues.find(Name);
    if (Iter != NamedValues.cend()) {
      return Values[Iter->second].Value;
    }
    return nullptr;
  }
};

#endif