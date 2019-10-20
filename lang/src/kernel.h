#pragma once

#include "util.h"
#include "snode.h"
#include "ir.h"

TLANG_NAMESPACE_BEGIN

class Program;

class Kernel {
 public:
  std::unique_ptr<IRNode> ir_holder;
  IRNode *ir;
  Program &program;
  FunctionType compiled;
  std::string name;
  struct Arg {
    DataType dt;
    bool is_nparray;
    std::size_t size;
  };
  std::vector<Arg> args;
  bool benchmarking;
  bool is_reduction;  // TODO: systematically treat all types of reduction
  bool grad;

  Kernel(Program &program,
         std::function<void()> func,
         std::string name = "",
         bool grad = false);

  void compile();

  void operator()();

  std::function<void()> func() {
    return std::function<void()>([&] { (*this)(); });
  }

  int insert_arg(DataType dt, bool is_nparray) {
    args.push_back({dt, is_nparray});
    return args.size() - 1;
  }

  void set_arg_float(int i, float64 d);

  void set_arg_int(int i, int64 d);

  void set_arg_nparray(int i, uint64 ptr, uint64 size);
};

TLANG_NAMESPACE_END
