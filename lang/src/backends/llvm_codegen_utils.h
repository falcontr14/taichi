#pragma once

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "llvm_jit.h"

TLANG_NAMESPACE_BEGIN

std::string type_name(llvm::Type *type);

bool check_func_call_signature(llvm::Value *func,
                               std::vector<Value *> arglist);

template <typename... Args>
inline bool check_func_call_signature(llvm::Value *func, Args &&... args) {
  return check_func_call_signature(func, {args...});
}


class ModuleBuilder {
 public:
  std::unique_ptr<Module> module;

  ModuleBuilder(std::unique_ptr<Module> &&module) : module(std::move(module)) {
  }

  llvm::Type *get_runtime_type(const std::string &name) {
    auto ty = module->getTypeByName("struct." + name);
    if (!ty) {
      TC_ERROR("Runtime type {} not found.", name);
    }
    return ty;
  }

  llvm::Function *get_runtime_function(const std::string &name) {
    auto f = module->getFunction(name);
    if (!f) {
      TC_ERROR("Runtime function {} not found.", name);
    }
    return f;
  }

  template <typename... Args>
  llvm::Value *call(llvm::IRBuilder<> *builder, const std::string &func_name, Args &&... args) {
    auto func = get_runtime_function(func_name);
    auto arglist = std::vector<Value *>({args...});
    check_func_call_signature(func, arglist);
    return builder->CreateCall(func, arglist);
  }
};

class RuntimeObject {
 public:
  std::string cls_name;
  llvm::Value *ptr;
  ModuleBuilder *mb;
  llvm::IRBuilder<> *builder;

  RuntimeObject(const std::string &cls_name,
                ModuleBuilder *mb,
                llvm::IRBuilder<> *builder,
                llvm::Value *init = nullptr)
      : cls_name(cls_name), mb(mb), builder(builder) {
    if (init == nullptr) {
      auto type = mb->get_runtime_type(cls_name);
      ptr = builder->CreateAlloca(type);
    } else {
      ptr = builder->CreateBitCast(
          init, llvm::PointerType::get(mb->get_runtime_type(cls_name), 0));
    }
  }

  llvm::Value *get(const std::string &field) {
    return call(fmt::format("get_{}", field));
  }

  llvm::Value *get(const std::string &field, llvm::Value *index) {
    return call(fmt::format("get_{}", field), index);
  }

  llvm::Value *get_ptr(const std::string &field) {
    return call(fmt::format("get_ptr_{}", field));
  }

  void set(const std::string &field, llvm::Value *val) {
    call(fmt::format("set_{}", field), val);
  }

  template <typename... Args>
  llvm::Value *call(const std::string &func_name, Args &&... args) {
    auto func = get_func(func_name);
    auto arglist = std::vector<Value *>({ptr, args...});
    check_func_call_signature(func, arglist);
    return builder->CreateCall(func, arglist);
  }

  llvm::Value *get_func(const std::string &func_name) const {
    return mb->get_runtime_function(fmt::format("{}_{}", cls_name, func_name));
  }
};

TLANG_NAMESPACE_END
