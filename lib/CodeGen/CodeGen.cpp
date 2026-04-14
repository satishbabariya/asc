#include "asc/CodeGen/CodeGen.h"
#include "asc/CodeGen/ConcurrencyLowering.h"
#include "asc/CodeGen/OwnershipLowering.h"
#include "asc/CodeGen/PanicLowering.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/All.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

namespace asc {

CodeGenerator::CodeGenerator(const CodeGenOptions &opts) : opts(opts) {
  // Initialize all LLVM targets.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllAsmPrinters();
}

CodeGenerator::~CodeGenerator() = default;

int CodeGenerator::generate(mlir::ModuleOp module) {
  // Step 1: Emit MLIR if requested.
  if (opts.emitKind == EmitKind::MLIR) {
    if (opts.outputFile.empty()) {
      return emitMLIR(module, llvm::outs()) ? 1 : 0;
    }
    std::error_code ec;
    llvm::raw_fd_ostream os(opts.outputFile, ec);
    if (ec) {
      llvm::errs() << "error: cannot open output file: " << ec.message() << "\n";
      return 2;
    }
    return emitMLIR(module, os) ? 1 : 0;
  }

  // Step 2: Run MLIR lowering passes.
  if (!runMLIRLowering(module))
    return 1;

  // Step 3: Translate to LLVM IR.
  if (!translateToLLVMIR(module))
    return 1;

  // Step 4: Emit LLVM IR if requested.
  if (opts.emitKind == EmitKind::LLVMIR) {
    if (opts.outputFile.empty()) {
      return emitLLVMIR(llvm::outs()) ? 1 : 0;
    }
    std::error_code ec;
    llvm::raw_fd_ostream os(opts.outputFile, ec);
    if (ec)
      return 2;
    return emitLLVMIR(os) ? 1 : 0;
  }

  // Step 5: Setup target machine.
  if (!setupTargetMachine())
    return 2;

  // Step 6: Run LLVM optimization passes.
  runLLVMOptPasses();

  // Step 7: Emit output.
  if (opts.outputFile.empty()) {
    return emitOutput(llvm::outs()) ? 1 : 0;
  }
  std::error_code ec;
  llvm::raw_fd_ostream os(opts.outputFile, ec, llvm::sys::fs::OF_None);
  if (ec) {
    llvm::errs() << "error: cannot open output file: " << ec.message() << "\n";
    return 2;
  }
  return emitOutput(os) ? 1 : 0;
}

bool CodeGenerator::runMLIRLowering(mlir::ModuleOp module) {
  mlir::PassManager pm(module.getContext());
  // Custom lowering passes erase unregistered ops (own.try_scope etc.)
  // created by PanicScopeWrap. Verifier must be off during this phase.
  pm.enableVerifier(false);

  // Custom lowering passes.
  pm.addPass(createPanicLoweringPass());         // try/catch → setjmp first
  pm.addPass(createOwnershipLoweringPass());     // then own.alloc/drop/move
  pm.addPass(createConcurrencyLoweringPass(llvm::Triple(opts.targetTriple)));

  // Canonicalization: constant folding, dead code elimination.
  pm.addPass(mlir::createCanonicalizerPass());

  // Standard MLIR-to-LLVM lowering.
  pm.addPass(mlir::createConvertSCFToCFPass());
  pm.addPass(mlir::createConvertFuncToLLVMPass());
  pm.addPass(mlir::createArithToLLVMConversionPass());
  pm.addPass(mlir::createConvertControlFlowToLLVMPass());

  return mlir::succeeded(pm.run(module));
}

bool CodeGenerator::translateToLLVMIR(mlir::ModuleOp module) {
  // Register ALL dialect-to-LLVM-IR translations (func, arith, cf, llvm, etc.)
  mlir::DialectRegistry registry;
  mlir::registerAllToLLVMIRTranslations(registry);
  module.getContext()->appendDialectRegistry(registry);

  llvmModule = mlir::translateModuleToLLVMIR(module, llvmContext);
  if (!llvmModule) {
    llvm::errs() << "error: failed to translate MLIR to LLVM IR\n";
    return false;
  }
  llvmModule->setTargetTriple(opts.targetTriple);

  // Warn about experimental GPU targets.
  llvm::Triple triple(opts.targetTriple);
  if (triple.isNVPTX() || triple.isAMDGCN()) {
    llvm::errs() << "warning: GPU target support is experimental\n";
  }

  // Add DWARF debug info if requested.
  if (opts.debugInfo) {
    llvmModule->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                              llvm::DEBUG_METADATA_VERSION);
    llvmModule->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);
    addDebugInfo();
  }

  // No-GC verification: scan for GC intrinsics (must never appear).
  for (auto &func : *llvmModule) {
    for (auto &bb : func) {
      for (auto &inst : bb) {
        if (auto *call = llvm::dyn_cast<llvm::CallInst>(&inst)) {
          if (auto *callee = call->getCalledFunction()) {
            llvm::StringRef name = callee->getName();
            if (name.starts_with("llvm.gcroot") ||
                name.starts_with("llvm.gcwrite") ||
                name.starts_with("llvm.gcread")) {
              llvm::errs() << "internal error: GC intrinsic found in output "
                           << "(asc is a no-GC compiler): " << name << "\n";
              return false;
            }
          }
        }
      }
    }
  }

  return true;
}

bool CodeGenerator::setupTargetMachine() {
  std::string error;
  const auto *target =
      llvm::TargetRegistry::lookupTarget(opts.targetTriple, error);
  if (!target) {
    llvm::errs() << "error: " << error << "\n";
    return false;
  }

  llvm::TargetOptions targetOpts;
  // Wasm targets need specific CPU and feature strings.
  std::string cpu = "generic";
  std::string features;
  llvm::Triple triple(opts.targetTriple);
  if (triple.isWasm()) {
    cpu = "generic";
    if (!opts.wasmFeatures.empty()) {
      // Use user-specified features (e.g. --wasm-features "+bulk-memory,+sign-ext").
      features = opts.wasmFeatures;
    } else {
      // Default features: bulk-memory for memcpy, mutable-globals for TLS,
      // sign-ext for integer operations.
      features = "+bulk-memory,+mutable-globals,+sign-ext";
    }
  }
  // Use O0 for the legacy PM codegen on all targets. Optimization is handled
  // by the new-PM in runLLVMOptPasses(). The legacy PM crashes at O2+ due to
  // pass scheduling issues in LLVM 18 (affects both Wasm and native backends).
  auto codegenOpt = llvm::CodeGenOptLevel::None;
  targetMachine.reset(target->createTargetMachine(
      opts.targetTriple, cpu, features, targetOpts, llvm::Reloc::PIC_,
      std::nullopt, codegenOpt));
  if (!targetMachine) {
    llvm::errs() << "error: could not create target machine\n";
    return false;
  }

  llvmModule->setDataLayout(targetMachine->createDataLayout());
  return true;
}

void CodeGenerator::runLLVMOptPasses() {
  if (opts.optLevel == OptLevel::O0)
    return;



  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;

  llvm::PassBuilder pb(targetMachine.get());
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::OptimizationLevel optLvl;
  switch (opts.optLevel) {
  case OptLevel::O0: optLvl = llvm::OptimizationLevel::O0; break;
  case OptLevel::O1: optLvl = llvm::OptimizationLevel::O1; break;
  case OptLevel::O2: optLvl = llvm::OptimizationLevel::O2; break;
  case OptLevel::O3: optLvl = llvm::OptimizationLevel::O3; break;
  case OptLevel::Os: optLvl = llvm::OptimizationLevel::Os; break;
  case OptLevel::Oz: optLvl = llvm::OptimizationLevel::Oz; break;
  }

  auto mpm = pb.buildPerModuleDefaultPipeline(optLvl);
  mpm.run(*llvmModule, mam);
}

bool CodeGenerator::emitOutput(llvm::raw_pwrite_stream &os) {
  switch (opts.emitKind) {
  case EmitKind::Wasm:
  case EmitKind::Object:
    return emitMachineCode(os, /*isAsm=*/false);
  case EmitKind::Wat:
  case EmitKind::Asm:
    return emitMachineCode(os, /*isAsm=*/true);
  case EmitKind::LLVMIR:
    return emitLLVMIR(os);
  case EmitKind::MLIR:
    return false; // already handled
  }
  return false;
}

bool CodeGenerator::emitMLIR(mlir::ModuleOp module, llvm::raw_ostream &os) {
  module.print(os);
  return false;
}

bool CodeGenerator::emitLLVMIR(llvm::raw_ostream &os) {
  llvmModule->print(os, nullptr);
  return false;
}

bool CodeGenerator::emitMachineCode(llvm::raw_pwrite_stream &os, bool isAsm) {
  llvm::legacy::PassManager pm;
  auto fileType = isAsm ? llvm::CodeGenFileType::AssemblyFile
                        : llvm::CodeGenFileType::ObjectFile;

  if (targetMachine->addPassesToEmitFile(pm, os, nullptr, fileType)) {
    llvm::errs() << "error: target cannot emit this file type\n";
    return true;
  }
  pm.run(*llvmModule);
  return false;
}

llvm::CodeGenOptLevel CodeGenerator::getLLVMOptLevel() const {
  switch (opts.optLevel) {
  case OptLevel::O0: return llvm::CodeGenOptLevel::None;
  case OptLevel::O1: return llvm::CodeGenOptLevel::Less;
  case OptLevel::O2: return llvm::CodeGenOptLevel::Default;
  case OptLevel::O3: return llvm::CodeGenOptLevel::Aggressive;
  case OptLevel::Os: return llvm::CodeGenOptLevel::Default;
  case OptLevel::Oz: return llvm::CodeGenOptLevel::Default;
  }
  return llvm::CodeGenOptLevel::Default;
}

unsigned CodeGenerator::getLLVMOptLevelNum() const {
  switch (opts.optLevel) {
  case OptLevel::O0: return 0;
  case OptLevel::O1: return 1;
  case OptLevel::O2: return 2;
  case OptLevel::O3: return 3;
  case OptLevel::Os: return 2;
  case OptLevel::Oz: return 2;
  }
  return 2;
}

void CodeGenerator::addDebugInfo() {
  // Create a DIBuilder for the module.
  llvm::DIBuilder dib(*llvmModule);

  // Use the input source file for debug info.
  std::string srcFile = opts.outputFile;
  std::string srcDir = ".";
  // Try to get the actual source file from the module metadata or opts.
  if (srcFile.empty()) srcFile = "<stdin>";
  auto *file = dib.createFile(srcFile, srcDir);
  auto *cu = dib.createCompileUnit(
      llvm::dwarf::DW_LANG_C, file, "asc 0.1.0",
      opts.optLevel != OptLevel::O0, /*Flags=*/"", /*RV=*/0);

  // Add subprogram info for each function.
  unsigned funcLine = 1;
  for (auto &func : *llvmModule) {
    if (func.isDeclaration())
      continue;

    // Try to get a real line number from the first instruction's debug loc.
    unsigned startLine = funcLine++;
    for (auto &bb : func) {
      for (auto &inst : bb) {
        if (auto dl = inst.getDebugLoc()) {
          startLine = dl.getLine();
          break;
        }
      }
      break;
    }

    auto *funcType = dib.createSubroutineType(dib.getOrCreateTypeArray({}));
    auto *sp = dib.createFunction(
        file, func.getName(), func.getName(), file,
        startLine, funcType, startLine,
        llvm::DINode::FlagPrototyped,
        llvm::DISubprogram::SPFlagDefinition);
    func.setSubprogram(sp);

    // Set debug location on instructions that don't already have one.
    unsigned lastLine = startLine;
    for (auto &bb : func) {
      for (auto &inst : bb) {
        if (auto dl = inst.getDebugLoc()) {
          lastLine = dl.getLine();
        } else {
          inst.setDebugLoc(llvm::DILocation::get(
              llvmContext, lastLine, 0, sp));
        }
      }
    }
  }

  dib.finalize();
}

} // namespace asc
