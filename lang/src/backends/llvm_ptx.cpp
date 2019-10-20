// A work-in-progress llvm backend

#include <taichi/common/util.h>
#include <taichi/io/io.h>
#include <set>

#include "cuda_context.h"
#include "../util.h"
#include "codegen_cuda.h"
#include "../program.h"
#include "../ir.h"

#if defined(TLANG_WITH_LLVM)

#include "codegen_llvm.h"

#endif

TLANG_NAMESPACE_BEGIN

#if defined(TLANG_WITH_LLVM)

using namespace llvm;

// NVVM IR Spec:
// https://docs.nvidia.com/cuda/archive/10.0/pdf/NVVM_IR_Specification.pdf

class CodeGenLLVMGPU : public CodeGenLLVM {
 public:
  int kernel_grid_dim;
  int kernel_block_dim;

  CodeGenLLVMGPU(CodeGenBase *codegen_base, Kernel *kernel)
      : CodeGenLLVM(codegen_base, kernel) {
  }

  FunctionType compile_module_to_executable() override {
#if defined(TLANG_WITH_CUDA)
    llvm::Function *func = module->getFunction(kernel_name);
    TC_ASSERT(func);

    /*******************************************************************
    Example annotation from llvm PTX doc:

    define void @kernel(float addrspace(1)* %A,
                        float addrspace(1)* %B,
                        float addrspace(1)* %C);

    !nvvm.annotations = !{!0}
    !0 = !{void (float addrspace(1)*,
                 float addrspace(1)*,
                 float addrspace(1)*)* @kernel, !"kernel", i32 1}
    *******************************************************************/

    // Mark kernel function as a CUDA __global__ function
    // Add the nvvm annotation that it is considered a kernel function.

    llvm::Metadata *md_args[] = {
        llvm::ValueAsMetadata::get(func),
        MDString::get(*llvm_context, "kernel"),
        llvm::ValueAsMetadata::get(tlctx->get_constant(1))};

    MDNode *md_node = MDNode::get(*llvm_context, md_args);

    module->getOrInsertNamedMetadata("nvvm.annotations")->addOperand(md_node);

    auto ptx = compile_module_to_ptx(module);
    auto cuda_kernel = cuda_context.compile(ptx, kernel_name);
    return [=](Context context) {
      cuda_context.launch(cuda_kernel, &context, kernel_grid_dim,
                          kernel_block_dim);
    };
#else
    TC_NOT_IMPLEMENTED;
    return nullptr;
#endif
  }

  void visit(PrintStmt *stmt) override {
    TC_ASSERT(stmt->width() == 1);

    auto value_type = tlctx->get_data_type(stmt->stmt->ret_type.data_type);

    std::string format;

    auto value = stmt->stmt->value;

    if (stmt->stmt->ret_type.data_type == DataType::i32) {
      format = "%d";
    } else if (stmt->stmt->ret_type.data_type == DataType::f32) {
      value_type = llvm::Type::getDoubleTy(*llvm_context);
      value = builder->CreateFPExt(value, value_type);
      format = "%f";
    } else if (stmt->stmt->ret_type.data_type == DataType::f64) {
      format = "%f";
    } else {
      TC_NOT_IMPLEMENTED
    }

    std::vector<llvm::Type *> types{value_type};
    auto stype = llvm::StructType::get(*llvm_context, types, false);
    auto values = builder->CreateAlloca(stype);
    auto value_ptr = builder->CreateGEP(
        values, {tlctx->get_constant(0), tlctx->get_constant(0)});
    builder->CreateStore(value, value_ptr);

    auto format_str = "[debug] " + stmt->str + " = " + format + "\n";

    stmt->value = ModuleBuilder::call(
        builder, "vprintf",
        builder->CreateGlobalStringPtr(format_str, "format_string"),
        builder->CreateBitCast(values,
                               llvm::Type::getInt8PtrTy(*llvm_context)));
  }

  void visit(RangeForStmt *for_stmt) override {
    if (offloaded) {
      create_naive_range_for(for_stmt);
    } else {
      offloaded = true;
      auto loop_begin = for_stmt->begin->as<ConstStmt>()->val[0].val_int32();
      auto loop_end = for_stmt->end->as<ConstStmt>()->val[0].val_int32();
      auto loop_block_dim = for_stmt->block_size;
      if (loop_block_dim == 0) {
        loop_block_dim = default_gpu_block_size;
      }
      kernel_grid_dim =
          (loop_end - loop_begin + loop_block_dim - 1) / loop_block_dim;
      kernel_block_dim = loop_block_dim;
      BasicBlock *body = BasicBlock::Create(*llvm_context, "loop_body", func);
      BasicBlock *after_loop = BasicBlock::Create(*llvm_context, "block", func);

      auto threadIdx =
          builder->CreateIntrinsic(Intrinsic::nvvm_read_ptx_sreg_tid_x, {}, {});
      auto blockIdx = builder->CreateIntrinsic(
          Intrinsic::nvvm_read_ptx_sreg_ctaid_x, {}, {});
      auto blockDim = builder->CreateIntrinsic(
          Intrinsic::nvvm_read_ptx_sreg_ntid_x, {}, {});

      auto loop_id = builder->CreateAdd(
          for_stmt->begin->value,
          builder->CreateAdd(threadIdx,
                             builder->CreateMul(blockIdx, blockDim)));

      builder->CreateStore(loop_id, for_stmt->loop_var->value);

      auto cond = builder->CreateICmp(
          llvm::CmpInst::Predicate::ICMP_SLT,
          builder->CreateLoad(for_stmt->loop_var->value), for_stmt->end->value);

      builder->CreateCondBr(cond, body, after_loop);
      {
        // body cfg
        builder->SetInsertPoint(body);
        for_stmt->body->accept(this);
        builder->CreateBr(after_loop);
      }

      // create_increment(for_stmt->loop_var->value, tlctx->get_constant(1));

      builder->SetInsertPoint(after_loop);
      // builder->CreateRetVoid();

      offloaded = false;
    }
  }
};

FunctionType GPUCodeGen::codegen_llvm() {
  return CodeGenLLVMGPU(this, kernel).gen();
}

#else

FunctionType GPUCodeGen::codegen_llvm() {
  TC_ERROR("LLVM not found");
}

#endif

TLANG_NAMESPACE_END
