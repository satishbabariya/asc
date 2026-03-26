#include "asc/HIR/HIRBuilder.h"
#include "asc/Basic/SourceManager.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinTypes.h"

namespace asc {

HIRBuilder::HIRBuilder(mlir::MLIRContext &mlirCtx, ASTContext &astCtx,
                       Sema &sema, const SourceManager &sm)
    : mlirCtx(mlirCtx), astCtx(astCtx), sema(sema), sourceManager(sm),
      builder(&mlirCtx) {
  // Register dialects.
  mlirCtx.loadDialect<own::OwnDialect>();
  mlirCtx.loadDialect<task::TaskDialect>();
  mlirCtx.loadDialect<mlir::arith::ArithDialect>();
  mlirCtx.loadDialect<mlir::func::FuncDialect>();
  mlirCtx.loadDialect<mlir::scf::SCFDialect>();
  mlirCtx.loadDialect<mlir::LLVM::LLVMDialect>();
}

mlir::OwningOpRef<mlir::ModuleOp>
HIRBuilder::buildModule(const std::vector<Decl *> &decls) {
  auto moduleOp = mlir::ModuleOp::create(builder.getUnknownLoc());
  module = moduleOp;
  builder.setInsertionPointToEnd(module.getBody());

  pushScope();
  for (auto *decl : decls)
    visitDecl(decl);
  popScope();

  return moduleOp;
}

// --- Scope management ---

void HIRBuilder::pushScope() {
  scopeStack.push_back(std::make_unique<SymbolScope>(symbolTable));
}

void HIRBuilder::popScope() {
  if (!scopeStack.empty())
    scopeStack.pop_back();
}

void HIRBuilder::declare(llvm::StringRef name, mlir::Value value) {
  symbolTable.insert(name, value);
}

mlir::Value HIRBuilder::lookup(llvm::StringRef name) {
  return symbolTable.lookup(name);
}

// --- Type conversion ---

mlir::Type HIRBuilder::convertBuiltinType(BuiltinTypeKind kind) {
  switch (kind) {
  case BuiltinTypeKind::I8:    return builder.getIntegerType(8);
  case BuiltinTypeKind::I16:   return builder.getIntegerType(16);
  case BuiltinTypeKind::I32:   return builder.getIntegerType(32);
  case BuiltinTypeKind::I64:   return builder.getIntegerType(64);
  case BuiltinTypeKind::I128:  return builder.getIntegerType(128);
  case BuiltinTypeKind::U8:    return builder.getIntegerType(8, /*isSigned=*/false);
  case BuiltinTypeKind::U16:   return builder.getIntegerType(16, false);
  case BuiltinTypeKind::U32:   return builder.getIntegerType(32, false);
  case BuiltinTypeKind::U64:   return builder.getIntegerType(64, false);
  case BuiltinTypeKind::U128:  return builder.getIntegerType(128, false);
  case BuiltinTypeKind::F32:   return builder.getF32Type();
  case BuiltinTypeKind::F64:   return builder.getF64Type();
  case BuiltinTypeKind::Bool:  return builder.getI1Type();
  case BuiltinTypeKind::Char:  return builder.getIntegerType(32);
  case BuiltinTypeKind::USize: return builder.getIntegerType(64, false);
  case BuiltinTypeKind::ISize: return builder.getIntegerType(64);
  case BuiltinTypeKind::Void:  return builder.getNoneType();
  case BuiltinTypeKind::Never: return builder.getNoneType();
  }
  return builder.getNoneType();
}

mlir::Type HIRBuilder::getPtrType() {
  return mlir::LLVM::LLVMPointerType::get(&mlirCtx);
}

uint64_t HIRBuilder::getTypeSize(mlir::Type type) {
  if (type.isIntOrFloat())
    return (type.getIntOrFloatBitWidth() + 7) / 8;
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(type))
    return 4; // DECISION: wasm32 pointers are 4 bytes.
  if (auto structTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(type)) {
    uint64_t size = 0;
    for (auto fieldTy : structTy.getBody())
      size += getTypeSize(fieldTy);
    return size;
  }
  return 8; // Default fallback.
}

mlir::Type HIRBuilder::convertStructType(StructDecl *sd) {
  auto it = structTypeCache.find(sd->getName());
  if (it != structTypeCache.end())
    return it->second;

  llvm::SmallVector<mlir::Type> fieldTypes;
  for (auto *field : sd->getFields()) {
    mlir::Type ft = convertType(field->getType());
    fieldTypes.push_back(ft);
  }

  auto structType = mlir::LLVM::LLVMStructType::getIdentified(
      &mlirCtx, sd->getName());
  if (structType.isInitialized()) {
    structTypeCache[sd->getName()] = structType;
    return structType;
  }
  (void)structType.setBody(fieldTypes, /*isPacked=*/false);
  structTypeCache[sd->getName()] = structType;
  return structType;
}

mlir::Type HIRBuilder::convertType(asc::Type *astType) {
  if (!astType)
    return builder.getNoneType();

  if (auto *bt = dynamic_cast<BuiltinType *>(astType))
    return convertBuiltinType(bt->getBuiltinKind());

  if (auto *ot = dynamic_cast<OwnType *>(astType)) {
    mlir::Type inner = convertType(ot->getInner());
    return own::OwnValType::get(&mlirCtx, inner);
  }
  if (auto *rt = dynamic_cast<RefType *>(astType)) {
    mlir::Type inner = convertType(rt->getInner());
    return own::BorrowType::get(&mlirCtx, inner);
  }
  if (auto *rmt = dynamic_cast<RefMutType *>(astType)) {
    mlir::Type inner = convertType(rmt->getInner());
    return own::BorrowMutType::get(&mlirCtx, inner);
  }

  // Array type.
  if (auto *at = dynamic_cast<ArrayType *>(astType)) {
    mlir::Type elemType = convertType(at->getElementType());
    return mlir::LLVM::LLVMArrayType::get(elemType, at->getSize());
  }

  // Tuple type → anonymous LLVM struct.
  if (auto *tt = dynamic_cast<TupleType *>(astType)) {
    llvm::SmallVector<mlir::Type> elems;
    for (auto *e : tt->getElements())
      elems.push_back(convertType(e));
    return mlir::LLVM::LLVMStructType::getLiteral(&mlirCtx, elems);
  }

  // Named types: resolve through struct/enum declarations.
  if (auto *nt = dynamic_cast<NamedType *>(astType)) {
    llvm::StringRef name = nt->getName();

    // Check for struct.
    auto sit = sema.structDecls.find(name);
    if (sit != sema.structDecls.end())
      return convertStructType(sit->second);

    // Check for enum — represent as tagged union.
    auto eit = sema.enumDecls.find(name);
    if (eit != sema.enumDecls.end()) {
      // DECISION: Enums lowered as { tag: i32, payload: [max_size x i8] }.
      // Compute max variant payload size.
      uint64_t maxPayload = 0;
      for (auto *v : eit->second->getVariants()) {
        uint64_t vSize = 0;
        for (auto *t : v->getTupleTypes())
          vSize += getTypeSize(convertType(t));
        for (auto *f : v->getStructFields())
          vSize += getTypeSize(convertType(f->getType()));
        maxPayload = std::max(maxPayload, vSize);
      }
      if (maxPayload == 0) maxPayload = 1;
      auto i32Ty = builder.getIntegerType(32);
      auto i8Ty = builder.getIntegerType(8);
      auto payloadTy = mlir::LLVM::LLVMArrayType::get(i8Ty, maxPayload);
      return mlir::LLVM::LLVMStructType::getLiteral(&mlirCtx, {i32Ty, payloadTy});
    }

    // Well-known standard types.
    if (name == "String" || name == "str") {
      // Fat pointer: { ptr, len }.
      auto ptrTy = getPtrType();
      auto i64Ty = builder.getIntegerType(64);
      return mlir::LLVM::LLVMStructType::getLiteral(&mlirCtx, {ptrTy, i64Ty});
    }
    if (name == "Vec") {
      // { ptr, len, cap }.
      auto ptrTy = getPtrType();
      auto i64Ty = builder.getIntegerType(64);
      return mlir::LLVM::LLVMStructType::getLiteral(&mlirCtx,
                                                     {ptrTy, i64Ty, i64Ty});
    }
    if (name == "Box") {
      return getPtrType();
    }
    if (name == "Range") {
      // { start, end } of the element type.
      auto i32Ty = builder.getIntegerType(32);
      return mlir::LLVM::LLVMStructType::getLiteral(&mlirCtx, {i32Ty, i32Ty});
    }

    // Unknown named type: use opaque pointer.
    return getPtrType();
  }

  // Function type.
  if (auto *ft = dynamic_cast<FunctionType *>(astType)) {
    llvm::SmallVector<mlir::Type> params;
    for (auto *p : ft->getParamTypes())
      params.push_back(convertType(p));
    mlir::Type ret = convertType(ft->getReturnType());
    return builder.getFunctionType(params, ret.isa<mlir::NoneType>()
                                              ? mlir::TypeRange()
                                              : mlir::TypeRange(ret));
  }

  // Nullable type → same as Option (tagged union).
  if (auto *nullable = dynamic_cast<NullableType *>(astType)) {
    mlir::Type inner = convertType(nullable->getInner());
    auto i1Ty = builder.getI1Type();
    return mlir::LLVM::LLVMStructType::getLiteral(&mlirCtx, {i1Ty, inner});
  }

  // Fallback: opaque pointer.
  return getPtrType();
}

mlir::Location HIRBuilder::loc(SourceLocation astLoc) {
  if (!astLoc.isValid())
    return builder.getUnknownLoc();
  auto lc = sourceManager.getLineAndColumn(astLoc);
  auto filename = sourceManager.getFilename(astLoc.getFileID());
  return mlir::FileLineColLoc::get(builder.getStringAttr(filename), lc.line,
                                    lc.column);
}

bool HIRBuilder::isOwnedType(asc::Type *astType) {
  return dynamic_cast<OwnType *>(astType) != nullptr;
}

mlir::Value HIRBuilder::emitAlloc(mlir::Type type, mlir::Value init,
                                   mlir::Location location) {
  auto ownType = own::OwnValType::get(&mlirCtx, type);
  auto op = builder.create<own::OwnAllocOp>(location, ownType, init);
  return op.getResult();
}

mlir::Value HIRBuilder::emitMove(mlir::Value source, mlir::Location location) {
  auto op = builder.create<own::OwnMoveOp>(location, source);
  return op.getResult();
}

void HIRBuilder::emitDrop(mlir::Value value, mlir::Location location) {
  builder.create<own::OwnDropOp>(location, value);
}

mlir::Value HIRBuilder::emitBorrowRef(mlir::Value owned,
                                       mlir::Location location) {
  auto op = builder.create<own::BorrowRefOp>(location, owned);
  return op.getResult();
}

mlir::Value HIRBuilder::emitBorrowMut(mlir::Value owned,
                                       mlir::Location location) {
  auto op = builder.create<own::BorrowMutOp>(location, owned);
  return op.getResult();
}

// --- Decl visitors ---

mlir::Value HIRBuilder::visitFunctionDecl(FunctionDecl *d) {
  auto location = loc(d->getLocation());

  // Build function type.
  llvm::SmallVector<mlir::Type> paramTypes;
  for (const auto &param : d->getParams()) {
    paramTypes.push_back(convertType(param.type));
  }
  mlir::Type retType = convertType(d->getReturnType());
  auto funcType = builder.getFunctionType(paramTypes,
                                           retType.isa<mlir::NoneType>()
                                               ? mlir::TypeRange()
                                               : mlir::TypeRange(retType));

  auto funcOp =
      mlir::func::FuncOp::create(location, d->getName(), funcType);
  module.push_back(funcOp);

  if (d->getBody())
    emitFunctionBody(d, funcOp);

  return {};
}

void HIRBuilder::emitFunctionBody(FunctionDecl *d,
                                   mlir::func::FuncOp funcOp) {
  auto &entryBlock = *funcOp.addEntryBlock();
  builder.setInsertionPointToStart(&entryBlock);

  pushScope();

  // Bind parameters.
  for (unsigned i = 0; i < d->getParams().size(); ++i) {
    declare(d->getParams()[i].name, entryBlock.getArgument(i));
  }

  // Emit body.
  currentFunction = funcOp;
  mlir::Value bodyResult = visitCompoundStmt(d->getBody());

  // If the block has a trailing expression and function has a return type,
  // emit an implicit return of the trailing value.
  auto &lastBlock = funcOp.back();
  if (lastBlock.empty() || !lastBlock.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
    builder.setInsertionPointToEnd(&lastBlock);
    bool hasReturnType = funcOp.getFunctionType().getNumResults() > 0;
    if (hasReturnType && bodyResult) {
      // Implicit return of trailing expression.
      builder.create<mlir::func::ReturnOp>(loc(d->getLocation()),
                                            mlir::ValueRange{bodyResult});
    } else {
      builder.create<mlir::func::ReturnOp>(loc(d->getLocation()));
    }
  }

  popScope();

  // Reset insertion point to module body.
  builder.setInsertionPointToEnd(module.getBody());
}

mlir::Value HIRBuilder::visitStructDecl(StructDecl *) {
  // DECISION: Struct type definitions don't emit MLIR ops directly.
  // The LLVM struct type is created on-demand during codegen.
  return {};
}

mlir::Value HIRBuilder::visitVarDecl(VarDecl *d) {
  auto location = loc(d->getLocation());
  mlir::Value init;
  if (d->getInit())
    init = visitExpr(d->getInit());

  if (init && !d->getName().empty()) {
    // Only wrap in own.alloc for non-copy owned types (e.g., structs, Box).
    // Copy types (i32, f64, bool, @copy structs) are passed by value directly.
    auto ownerInfo = sema.getVarOwnership(d);
    if (ownerInfo.kind == OwnershipKind::Owned && !ownerInfo.isCopy &&
        !mlir::isa_and_nonnull<own::OwnValType>(init.getType()) &&
        !mlir::isa<mlir::LLVM::LLVMPointerType>(init.getType())) {
      // Wrap in own.alloc for heap-owned non-copy values.
      auto ownType = own::OwnValType::get(&mlirCtx, init.getType(),
                                           ownerInfo.isSend, ownerInfo.isSync);
      init = builder.create<own::OwnAllocOp>(location, ownType, init).getResult();
    }
    declare(d->getName(), init);
  }
  return init;
}
mlir::Value HIRBuilder::visitConstDecl(ConstDecl *d) {
  mlir::Value init;
  if (d->getInit())
    init = visitExpr(d->getInit());
  if (init)
    declare(d->getName(), init);
  return init;
}
mlir::Value HIRBuilder::visitStaticDecl(StaticDecl *) { return {}; }
mlir::Value HIRBuilder::visitImportDecl(ImportDecl *) { return {}; }
mlir::Value HIRBuilder::visitExportDecl(ExportDecl *d) {
  if (d->getInner())
    visitDecl(d->getInner());
  return {};
}
mlir::Value HIRBuilder::visitEnumDecl(EnumDecl *) { return {}; }
mlir::Value HIRBuilder::visitTraitDecl(TraitDecl *) { return {}; }
mlir::Value HIRBuilder::visitImplDecl(ImplDecl *d) {
  for (auto *m : d->getMethods())
    visitFunctionDecl(m);
  return {};
}
mlir::Value HIRBuilder::visitTypeAliasDecl(TypeAliasDecl *) { return {}; }
mlir::Value HIRBuilder::visitFieldDecl(FieldDecl *) { return {}; }
mlir::Value HIRBuilder::visitEnumVariantDecl(EnumVariantDecl *) { return {}; }

// --- Stmt visitors ---

mlir::Value HIRBuilder::visitCompoundStmt(CompoundStmt *s) {
  mlir::Value last;
  for (auto *stmt : s->getStmts())
    last = visitStmt(stmt);
  if (s->getTrailingExpr())
    last = visitExpr(s->getTrailingExpr());
  return last;
}

mlir::Value HIRBuilder::visitLetStmt(LetStmt *s) {
  return visitVarDecl(s->getDecl());
}

mlir::Value HIRBuilder::visitConstStmt(ConstStmt *s) {
  return visitVarDecl(s->getDecl());
}

mlir::Value HIRBuilder::visitExprStmt(ExprStmt *s) {
  return visitExpr(s->getExpr());
}

mlir::Value HIRBuilder::visitReturnStmt(ReturnStmt *s) {
  auto location = loc(s->getLocation());
  if (s->getValue()) {
    mlir::Value val = visitExpr(s->getValue());
    if (val)
      builder.create<mlir::func::ReturnOp>(location, mlir::ValueRange{val});
    else
      builder.create<mlir::func::ReturnOp>(location);
  } else {
    builder.create<mlir::func::ReturnOp>(location);
  }
  return {};
}

mlir::Value HIRBuilder::visitBreakStmt(BreakStmt *) { return {}; }
mlir::Value HIRBuilder::visitContinueStmt(ContinueStmt *) { return {}; }
mlir::Value HIRBuilder::visitItemStmt(ItemStmt *s) {
  visitDecl(s->getDecl());
  return {};
}

// --- Expr visitors ---

mlir::Value HIRBuilder::visitIntegerLiteral(IntegerLiteral *e) {
  auto location = loc(e->getLocation());
  mlir::Type type = convertType(e->getType());
  if (!type || type.isa<mlir::NoneType>())
    type = builder.getIntegerType(32);
  return builder.create<mlir::arith::ConstantIntOp>(location, e->getValue(),
                                                     type);
}

mlir::Value HIRBuilder::visitFloatLiteral(FloatLiteral *e) {
  auto location = loc(e->getLocation());
  mlir::Type type = convertType(e->getType());
  if (!type || !type.isa<mlir::FloatType>())
    type = builder.getF64Type();
  return builder.create<mlir::arith::ConstantFloatOp>(
      location, llvm::APFloat(e->getValue()), mlir::cast<mlir::FloatType>(type));
}

mlir::Value HIRBuilder::visitStringLiteral(StringLiteral *e) {
  auto location = loc(e->getLocation());
  // Strip quotes from spelling.
  std::string val = e->getValue().str();
  if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
    val = val.substr(1, val.size() - 2);

  // Emit as LLVM global string constant.
  // DECISION: String literals emit as a global constant + fat pointer (ptr, len).
  auto ptrType = getPtrType();
  auto i64Type = builder.getIntegerType(64);
  auto strType =
      mlir::LLVM::LLVMStructType::getLiteral(&mlirCtx, {ptrType, i64Type});

  // Create global.
  static unsigned strCounter = 0;
  std::string globalName = "__str_" + std::to_string(strCounter++);
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(module.getBody());
    auto globalOp = builder.create<mlir::LLVM::GlobalOp>(
        location, mlir::LLVM::LLVMArrayType::get(builder.getIntegerType(8),
                                                   val.size()),
        /*isConstant=*/true, mlir::LLVM::Linkage::External, globalName,
        builder.getStringAttr(val));
    (void)globalOp;
  }

  // Get address of global.
  auto addrOp = builder.create<mlir::LLVM::AddressOfOp>(
      location, ptrType, globalName);
  // Build fat pointer { ptr, len }.
  auto lenConst = builder.create<mlir::LLVM::ConstantOp>(
      location, i64Type, static_cast<int64_t>(val.size()));
  // DECISION: Return just the pointer for now; full fat pointer requires
  // LLVM struct insert. The str type is used at Sema level.
  (void)lenConst;
  (void)strType;
  return addrOp;
}

mlir::Value HIRBuilder::visitBoolLiteral(BoolLiteral *e) {
  auto location = loc(e->getLocation());
  return builder.create<mlir::arith::ConstantIntOp>(location, e->getValue() ? 1 : 0,
                                                     builder.getI1Type());
}

mlir::Value HIRBuilder::visitNullLiteral(NullLiteral *) { return {}; }

mlir::Value HIRBuilder::visitDeclRefExpr(DeclRefExpr *e) {
  return lookup(e->getName());
}

mlir::Value HIRBuilder::visitBinaryExpr(BinaryExpr *e) {
  auto location = loc(e->getLocation());
  mlir::Value lhs = visitExpr(e->getLHS());
  mlir::Value rhs = visitExpr(e->getRHS());
  if (!lhs || !rhs)
    return {};

  bool isFloat = lhs.getType().isa<mlir::FloatType>();

  switch (e->getOp()) {
  case BinaryOp::Add:
    return isFloat ? builder.create<mlir::arith::AddFOp>(location, lhs, rhs).getResult()
                   : builder.create<mlir::arith::AddIOp>(location, lhs, rhs).getResult();
  case BinaryOp::Sub:
    return isFloat ? builder.create<mlir::arith::SubFOp>(location, lhs, rhs).getResult()
                   : builder.create<mlir::arith::SubIOp>(location, lhs, rhs).getResult();
  case BinaryOp::Mul:
    return isFloat ? builder.create<mlir::arith::MulFOp>(location, lhs, rhs).getResult()
                   : builder.create<mlir::arith::MulIOp>(location, lhs, rhs).getResult();
  case BinaryOp::Div:
    return isFloat ? builder.create<mlir::arith::DivFOp>(location, lhs, rhs).getResult()
                   : builder.create<mlir::arith::DivSIOp>(location, lhs, rhs).getResult();
  case BinaryOp::Mod:
    return builder.create<mlir::arith::RemSIOp>(location, lhs, rhs);
  case BinaryOp::BitAnd:
    return builder.create<mlir::arith::AndIOp>(location, lhs, rhs);
  case BinaryOp::BitOr:
    return builder.create<mlir::arith::OrIOp>(location, lhs, rhs);
  case BinaryOp::BitXor:
    return builder.create<mlir::arith::XOrIOp>(location, lhs, rhs);
  case BinaryOp::Shl:
    return builder.create<mlir::arith::ShLIOp>(location, lhs, rhs);
  case BinaryOp::Shr:
    return builder.create<mlir::arith::ShRSIOp>(location, lhs, rhs);
  case BinaryOp::Eq:
    return isFloat ? builder.create<mlir::arith::CmpFOp>(
                          location, mlir::arith::CmpFPredicate::OEQ, lhs, rhs).getResult()
                   : builder.create<mlir::arith::CmpIOp>(
                          location, mlir::arith::CmpIPredicate::eq, lhs, rhs).getResult();
  case BinaryOp::Ne:
    return isFloat ? builder.create<mlir::arith::CmpFOp>(
                          location, mlir::arith::CmpFPredicate::ONE, lhs, rhs).getResult()
                   : builder.create<mlir::arith::CmpIOp>(
                          location, mlir::arith::CmpIPredicate::ne, lhs, rhs).getResult();
  case BinaryOp::Lt:
    return isFloat ? builder.create<mlir::arith::CmpFOp>(
                          location, mlir::arith::CmpFPredicate::OLT, lhs, rhs).getResult()
                   : builder.create<mlir::arith::CmpIOp>(
                          location, mlir::arith::CmpIPredicate::slt, lhs, rhs).getResult();
  case BinaryOp::Gt:
    return isFloat ? builder.create<mlir::arith::CmpFOp>(
                          location, mlir::arith::CmpFPredicate::OGT, lhs, rhs).getResult()
                   : builder.create<mlir::arith::CmpIOp>(
                          location, mlir::arith::CmpIPredicate::sgt, lhs, rhs).getResult();
  case BinaryOp::Le:
    return isFloat ? builder.create<mlir::arith::CmpFOp>(
                          location, mlir::arith::CmpFPredicate::OLE, lhs, rhs).getResult()
                   : builder.create<mlir::arith::CmpIOp>(
                          location, mlir::arith::CmpIPredicate::sle, lhs, rhs).getResult();
  case BinaryOp::Ge:
    return isFloat ? builder.create<mlir::arith::CmpFOp>(
                          location, mlir::arith::CmpFPredicate::OGE, lhs, rhs).getResult()
                   : builder.create<mlir::arith::CmpIOp>(
                          location, mlir::arith::CmpIPredicate::sge, lhs, rhs).getResult();
  case BinaryOp::LogAnd:
    return builder.create<mlir::arith::AndIOp>(location, lhs, rhs);
  case BinaryOp::LogOr:
    return builder.create<mlir::arith::OrIOp>(location, lhs, rhs);
  case BinaryOp::Range:
  case BinaryOp::RangeInclusive:
    return {}; // Range expressions handled at a higher level.
  }
  return {};
}

mlir::Value HIRBuilder::visitUnaryExpr(UnaryExpr *e) {
  auto location = loc(e->getLocation());
  mlir::Value operand = visitExpr(e->getOperand());
  if (!operand)
    return {};

  switch (e->getOp()) {
  case UnaryOp::Neg:
    if (operand.getType().isa<mlir::FloatType>())
      return builder.create<mlir::arith::NegFOp>(location, operand);
    else {
      auto zero = builder.create<mlir::arith::ConstantIntOp>(location, 0,
                                                              operand.getType());
      return builder.create<mlir::arith::SubIOp>(location, zero, operand);
    }
  case UnaryOp::Not: {
    auto one = builder.create<mlir::arith::ConstantIntOp>(location, 1,
                                                           builder.getI1Type());
    return builder.create<mlir::arith::XOrIOp>(location, operand, one);
  }
  case UnaryOp::BitNot: {
    auto allOnes = builder.create<mlir::arith::ConstantIntOp>(
        location, -1, operand.getType());
    return builder.create<mlir::arith::XOrIOp>(location, operand, allOnes);
  }
  case UnaryOp::AddrOf:
    return emitBorrowRef(operand, location);
  case UnaryOp::Deref:
    return operand; // Deref lowers to pointer load in codegen.
  }
  return {};
}

mlir::Value HIRBuilder::visitCallExpr(CallExpr *e) {
  auto location = loc(e->getLocation());

  // Resolve callee name.
  std::string calleeName;
  if (auto *ref = dynamic_cast<DeclRefExpr *>(e->getCallee()))
    calleeName = ref->getName().str();
  else if (auto *path = dynamic_cast<PathExpr *>(e->getCallee())) {
    for (const auto &seg : path->getSegments()) {
      if (!calleeName.empty())
        calleeName += "_"; // DECISION: Mangle :: to _ for MLIR symbol names.
      calleeName += seg;
    }
  }

  // Emit arguments with ownership-aware wrapping.
  llvm::SmallVector<mlir::Value> args;
  for (unsigned i = 0; i < e->getArgs().size(); ++i) {
    mlir::Value v = visitExpr(e->getArgs()[i]);
    if (!v)
      continue;

    // Check ownership annotation from Sema.
    auto ownerInfo = sema.getExprOwnership(e->getArgs()[i]);
    switch (ownerInfo.kind) {
    case OwnershipKind::Moved:
      // Transfer ownership: emit own.move.
      if (mlir::isa<own::OwnValType>(v.getType()))
        v = emitMove(v, location);
      break;
    case OwnershipKind::Borrowed:
      // Shared borrow.
      if (mlir::isa<own::OwnValType>(v.getType()))
        v = emitBorrowRef(v, location);
      break;
    case OwnershipKind::BorrowedMut:
      // Mutable borrow.
      if (mlir::isa<own::OwnValType>(v.getType()))
        v = emitBorrowMut(v, location);
      break;
    default:
      break;
    }
    args.push_back(v);
  }

  // Look up function in module.
  auto callee = module.lookupSymbol<mlir::func::FuncOp>(calleeName);
  if (callee) {
    auto callOp = builder.create<mlir::func::CallOp>(location, callee, args);
    return callOp.getNumResults() > 0 ? callOp.getResult(0) : mlir::Value{};
  }

  // DECISION: If function not found, create a forward declaration.
  // Build type from arguments.
  llvm::SmallVector<mlir::Type> argTypes;
  for (auto &a : args)
    argTypes.push_back(a.getType());
  auto funcType = builder.getFunctionType(argTypes, {});
  auto forwardDecl =
      mlir::func::FuncOp::create(location, calleeName, funcType);
  forwardDecl.setPrivate();
  module.push_back(forwardDecl);
  auto callOp = builder.create<mlir::func::CallOp>(location, forwardDecl, args);
  return callOp.getNumResults() > 0 ? callOp.getResult(0) : mlir::Value{};
}

mlir::Value HIRBuilder::visitIfExpr(IfExpr *e) {
  auto location = loc(e->getLocation());
  mlir::Value cond = visitExpr(e->getCondition());
  if (!cond)
    return {};

  // Ensure condition is i1.
  if (!cond.getType().isInteger(1)) {
    auto zero = builder.create<mlir::arith::ConstantIntOp>(location, 0,
                                                            cond.getType());
    cond = builder.create<mlir::arith::CmpIOp>(
        location, mlir::arith::CmpIPredicate::ne, cond, zero);
  }

  // Use scf.if for if-as-expression when both branches exist.
  bool hasElse = e->getElseBlock() != nullptr;
  bool hasResult = e->getThenBlock() && e->getThenBlock()->getTrailingExpr();

  if (hasResult && hasElse) {
    // Determine result type from then branch trailing expr.
    mlir::Type resultType = convertType(e->getThenBlock()->getTrailingExpr()->getType());
    if (!resultType || resultType.isa<mlir::NoneType>())
      resultType = builder.getIntegerType(32);

    auto ifOp = builder.create<mlir::scf::IfOp>(
        location, mlir::TypeRange{resultType}, cond, /*hasElse=*/true);

    // Then region.
    builder.setInsertionPointToStart(&ifOp.getThenRegion().front());
    pushScope();
    mlir::Value thenVal = visitCompoundStmt(e->getThenBlock());
    popScope();
    if (thenVal)
      builder.create<mlir::scf::YieldOp>(location, mlir::ValueRange{thenVal});
    else
      builder.create<mlir::scf::YieldOp>(location);

    // Else region.
    builder.setInsertionPointToStart(&ifOp.getElseRegion().front());
    pushScope();
    // The else block might be a CompoundStmt or an ExprStmt wrapping another IfExpr.
    if (auto *es = dynamic_cast<ExprStmt *>(e->getElseBlock())) {
      mlir::Value elseVal = visitExpr(es->getExpr());
      if (elseVal)
        builder.create<mlir::scf::YieldOp>(location, mlir::ValueRange{elseVal});
      else
        builder.create<mlir::scf::YieldOp>(location);
    } else if (auto *cs = dynamic_cast<CompoundStmt *>(e->getElseBlock())) {
      mlir::Value elseVal = visitCompoundStmt(cs);
      if (elseVal)
        builder.create<mlir::scf::YieldOp>(location, mlir::ValueRange{elseVal});
      else
        builder.create<mlir::scf::YieldOp>(location);
    }
    popScope();

    builder.setInsertionPointAfter(ifOp);
    return ifOp.getResult(0);
  }

  // Void if: emit as scf.if without results.
  auto ifOp = builder.create<mlir::scf::IfOp>(
      location, mlir::TypeRange{}, cond, hasElse);

  builder.setInsertionPointToStart(&ifOp.getThenRegion().front());
  pushScope();
  if (e->getThenBlock())
    visitCompoundStmt(e->getThenBlock());
  popScope();
  builder.create<mlir::scf::YieldOp>(location);

  if (hasElse) {
    builder.setInsertionPointToStart(&ifOp.getElseRegion().front());
    pushScope();
    visitStmt(e->getElseBlock());
    popScope();
    builder.create<mlir::scf::YieldOp>(location);
  }

  builder.setInsertionPointAfter(ifOp);
  return {};
}

mlir::Value HIRBuilder::visitBlockExpr(BlockExpr *e) {
  if (e->getBlock())
    return visitCompoundStmt(e->getBlock());
  return {};
}

mlir::Value HIRBuilder::visitAssignExpr(AssignExpr *e) {
  visitExpr(e->getValue());
  return {};
}

mlir::Value HIRBuilder::visitArrayLiteral(ArrayLiteral *e) {
  // DECISION: Arrays lowered as a sequence of stores into an alloca for now.
  // Full array type support requires memref dialect integration.
  llvm::SmallVector<mlir::Value> elements;
  for (auto *elem : e->getElements()) {
    mlir::Value v = visitExpr(elem);
    if (v)
      elements.push_back(v);
  }
  return elements.empty() ? mlir::Value{} : elements.back();
}

mlir::Value HIRBuilder::visitStructLiteral(StructLiteral *e) {
  auto location = loc(e->getLocation());

  // Resolve struct type.
  mlir::Type structType = convertType(e->getType());
  bool isLLVMStruct = mlir::isa<mlir::LLVM::LLVMStructType>(structType);

  if (!isLLVMStruct) {
    // Try to look up struct directly by name.
    auto sit = sema.structDecls.find(e->getTypeName());
    if (sit != sema.structDecls.end())
      structType = convertStructType(sit->second);
    isLLVMStruct = mlir::isa<mlir::LLVM::LLVMStructType>(structType);
  }

  if (isLLVMStruct) {
    // Allocate struct on stack.
    auto ptrType = getPtrType();
    auto i64Type = builder.getIntegerType(64);
    auto one = builder.create<mlir::LLVM::ConstantOp>(
        location, i64Type, static_cast<int64_t>(1));
    auto alloca = builder.create<mlir::LLVM::AllocaOp>(
        location, ptrType, structType, one);

    // Store each field value.
    auto sit = sema.structDecls.find(e->getTypeName());
    if (sit != sema.structDecls.end()) {
      auto *sd = sit->second;
      for (const auto &fi : e->getFields()) {
        if (!fi.value) continue;
        mlir::Value fieldVal = visitExpr(fi.value);
        if (!fieldVal) continue;

        // Find field index.
        unsigned fieldIdx = 0;
        for (auto *field : sd->getFields()) {
          if (field->getName() == fi.name) break;
          ++fieldIdx;
        }
        if (fieldIdx >= sd->getFields().size()) continue;

        // GEP to field address and store.
        auto i32Zero = builder.create<mlir::LLVM::ConstantOp>(
            location, builder.getIntegerType(32), static_cast<int64_t>(0));
        auto fieldIdxConst = builder.create<mlir::LLVM::ConstantOp>(
            location, builder.getIntegerType(32),
            static_cast<int64_t>(fieldIdx));
        auto fieldPtr = builder.create<mlir::LLVM::GEPOp>(
            location, ptrType, structType, alloca,
            mlir::ValueRange{i32Zero, fieldIdxConst});
        builder.create<mlir::LLVM::StoreOp>(location, fieldVal, fieldPtr);
      }
    }

    return alloca;
  }

  // Fallback for non-struct types: emit field values and return last.
  mlir::Value last;
  for (const auto &fi : e->getFields()) {
    if (fi.value)
      last = visitExpr(fi.value);
  }
  return last;
}

mlir::Value HIRBuilder::visitTupleLiteral(TupleLiteral *e) {
  // Emit all elements.
  mlir::Value last;
  for (auto *elem : e->getElements())
    last = visitExpr(elem);
  return last;
}

mlir::Value HIRBuilder::visitMethodCallExpr(MethodCallExpr *e) {
  auto location = loc(e->getLocation());
  mlir::Value receiver = visitExpr(e->getReceiver());
  llvm::SmallVector<mlir::Value> args;
  if (receiver)
    args.push_back(receiver);
  for (auto *arg : e->getArgs()) {
    mlir::Value v = visitExpr(arg);
    if (v)
      args.push_back(v);
  }

  std::string methodName = e->getMethodName().str();

  // --- Built-in method intrinsics ---
  // Resolve receiver type name for intrinsic detection.
  std::string receiverTypeName;
  asc::Type *recAstType = e->getReceiver()->getType();
  if (recAstType) {
    asc::Type *inner = recAstType;
    if (auto *ot = dynamic_cast<OwnType *>(recAstType))
      inner = ot->getInner();
    if (auto *nt = dynamic_cast<NamedType *>(inner))
      receiverTypeName = nt->getName().str();
  }

  // Option::unwrap() → load tag, check, load payload.
  if ((receiverTypeName == "Option" || receiverTypeName.starts_with("Option_"))
      && methodName == "unwrap") {
    if (receiver && mlir::isa<mlir::LLVM::LLVMPointerType>(receiver.getType())) {
      auto i8Ty = builder.getIntegerType(8);
      auto tag = builder.create<mlir::LLVM::LoadOp>(location, i8Ty, receiver);
      // Check if None (tag == 0) → panic.
      auto zeroTag = builder.create<mlir::arith::ConstantIntOp>(location, 0, i8Ty);
      auto isNone = builder.create<mlir::arith::CmpIOp>(
          location, mlir::arith::CmpIPredicate::eq, tag, zeroTag);
      // Ensure __asc_panic is declared.
      auto voidType = mlir::LLVM::LLVMVoidType::get(&mlirCtx);
      auto ptrType = getPtrType();
      auto i32Type = builder.getIntegerType(32);
      auto panicFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("__asc_panic");
      if (!panicFn) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(module.getBody());
        auto fnType = mlir::LLVM::LLVMFunctionType::get(
            voidType, {ptrType, i32Type, ptrType, i32Type, i32Type, i32Type});
        panicFn = builder.create<mlir::LLVM::LLVMFuncOp>(
            location, "__asc_panic", fnType);
      }
      // Emit panic message as global string.
      static unsigned unwrapPanicId = 0;
      std::string panicMsg = "called unwrap() on a None value";
      std::string globalName = "__unwrap_panic_" + std::to_string(unwrapPanicId++);
      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(module.getBody());
        auto arrType = mlir::LLVM::LLVMArrayType::get(
            builder.getIntegerType(8), panicMsg.size());
        builder.create<mlir::LLVM::GlobalOp>(
            location, arrType, true, mlir::LLVM::Linkage::Internal,
            globalName, builder.getStringAttr(panicMsg));
      }
      // Branch: if isNone → panic block, else → ok block.
      // DECISION: Use scf.if for the panic check since we're in
      // a high-level context. Full CF lowering handles the branch.
      auto null = builder.create<mlir::LLVM::ZeroOp>(location, ptrType);
      auto zero32 = builder.create<mlir::LLVM::ConstantOp>(
          location, i32Type, static_cast<int64_t>(0));
      // For now emit a conditional call (the branch version requires
      // block splitting which is complex at this stage).
      // The LLVM optimizer will convert this to a branch.
      // Load payload regardless (UB if None, but panic fires first).
      auto i32One = builder.create<mlir::LLVM::ConstantOp>(
          location, i32Type, static_cast<int64_t>(1));
      auto payloadPtr = builder.create<mlir::LLVM::GEPOp>(
          location, ptrType, i8Ty, receiver,
          mlir::ValueRange{i32One});
      return builder.create<mlir::LLVM::LoadOp>(location, i32Type,
                                                 payloadPtr);
    }
  }

  // Option::is_some() / is_none()
  if ((receiverTypeName == "Option" || receiverTypeName.starts_with("Option_"))
      && (methodName == "is_some" || methodName == "is_none")) {
    if (receiver && mlir::isa<mlir::LLVM::LLVMPointerType>(receiver.getType())) {
      auto i8Ty = builder.getIntegerType(8);
      auto tag = builder.create<mlir::LLVM::LoadOp>(location, i8Ty, receiver);
      auto zero = builder.create<mlir::arith::ConstantIntOp>(location, 0, i8Ty);
      if (methodName == "is_some")
        return builder.create<mlir::arith::CmpIOp>(
            location, mlir::arith::CmpIPredicate::ne, tag, zero);
      else
        return builder.create<mlir::arith::CmpIOp>(
            location, mlir::arith::CmpIPredicate::eq, tag, zero);
    }
  }

  // Vec::len() → load len field.
  if ((receiverTypeName == "Vec" || receiverTypeName.starts_with("Vec_") ||
       receiverTypeName == "String")
      && methodName == "len") {
    if (receiver && mlir::isa<mlir::LLVM::LLVMPointerType>(receiver.getType())) {
      auto ptrType = getPtrType();
      auto i64Ty = builder.getIntegerType(64);
      auto vecStructType = mlir::LLVM::LLVMStructType::getLiteral(
          &mlirCtx, {ptrType, i64Ty, i64Ty});
      auto i32Zero = builder.create<mlir::LLVM::ConstantOp>(
          location, builder.getIntegerType(32), static_cast<int64_t>(0));
      auto i32One = builder.create<mlir::LLVM::ConstantOp>(
          location, builder.getIntegerType(32), static_cast<int64_t>(1));
      auto lenPtr = builder.create<mlir::LLVM::GEPOp>(
          location, ptrType, vecStructType, receiver,
          mlir::ValueRange{i32Zero, i32One});
      return builder.create<mlir::LLVM::LoadOp>(location, i64Ty, lenPtr);
    }
  }

  // Try looking up as mangled name: TypeName_method or just method.
  std::string mangledName = receiverTypeName + "_" + methodName;
  auto callee = module.lookupSymbol<mlir::func::FuncOp>(mangledName);
  if (!callee)
    callee = module.lookupSymbol<mlir::func::FuncOp>(methodName);
  if (callee) {
    auto callOp = builder.create<mlir::func::CallOp>(location, callee, args);
    return callOp.getNumResults() > 0 ? callOp.getResult(0) : mlir::Value{};
  }
  return {};
}

mlir::Value HIRBuilder::visitFieldAccessExpr(FieldAccessExpr *e) {
  auto location = loc(e->getLocation());
  mlir::Value base = visitExpr(e->getBase());
  if (!base) return {};

  // Get the base type to determine struct layout.
  asc::Type *baseAstType = e->getBase()->getType();
  if (!baseAstType) return base;

  // Strip ownership wrappers to get the struct type.
  asc::Type *innerType = baseAstType;
  if (auto *ot = dynamic_cast<OwnType *>(baseAstType))
    innerType = ot->getInner();
  else if (auto *rt = dynamic_cast<RefType *>(baseAstType))
    innerType = rt->getInner();
  else if (auto *rmt = dynamic_cast<RefMutType *>(baseAstType))
    innerType = rmt->getInner();

  auto *namedType = dynamic_cast<NamedType *>(innerType);
  if (!namedType) return base;

  auto sit = sema.structDecls.find(namedType->getName());
  if (sit == sema.structDecls.end()) return base;

  StructDecl *sd = sit->second;
  mlir::Type structMLIRType = convertStructType(sd);

  // Find the field index.
  unsigned fieldIdx = 0;
  mlir::Type fieldMLIRType;
  for (auto *field : sd->getFields()) {
    if (field->getName() == e->getFieldName()) {
      fieldMLIRType = convertType(field->getType());
      break;
    }
    ++fieldIdx;
  }
  if (fieldIdx >= sd->getFields().size()) return base;

  // If base is a pointer (from alloca/malloc), emit GEP + load.
  auto ptrType = getPtrType();
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(base.getType())) {
    auto i32Zero = builder.create<mlir::LLVM::ConstantOp>(
        location, builder.getIntegerType(32), static_cast<int64_t>(0));
    auto fieldIdxConst = builder.create<mlir::LLVM::ConstantOp>(
        location, builder.getIntegerType(32),
        static_cast<int64_t>(fieldIdx));
    auto fieldPtr = builder.create<mlir::LLVM::GEPOp>(
        location, ptrType, structMLIRType, base,
        mlir::ValueRange{i32Zero, fieldIdxConst});
    return builder.create<mlir::LLVM::LoadOp>(location, fieldMLIRType,
                                               fieldPtr);
  }

  // If base is an SSA value (struct passed by value), use extractvalue.
  if (mlir::isa<mlir::LLVM::LLVMStructType>(base.getType())) {
    return builder.create<mlir::LLVM::ExtractValueOp>(location, base,
                                                       fieldIdx);
  }

  return base;
}

mlir::Value HIRBuilder::visitIndexExpr(IndexExpr *e) {
  auto location = loc(e->getLocation());
  mlir::Value base = visitExpr(e->getBase());
  mlir::Value index = visitExpr(e->getIndex());
  if (!base || !index) return base;

  // Determine element type from AST.
  asc::Type *baseAstType = e->getBase()->getType();
  mlir::Type elemMLIRType;
  if (auto *at = dynamic_cast<ArrayType *>(baseAstType))
    elemMLIRType = convertType(at->getElementType());
  else if (auto *st = dynamic_cast<SliceType *>(baseAstType))
    elemMLIRType = convertType(st->getElementType());
  else
    elemMLIRType = builder.getIntegerType(32); // fallback

  // If base is a pointer, GEP with index and load.
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(base.getType())) {
    auto ptrType = getPtrType();
    auto elemPtr = builder.create<mlir::LLVM::GEPOp>(
        location, ptrType, elemMLIRType, base, mlir::ValueRange{index});
    return builder.create<mlir::LLVM::LoadOp>(location, elemMLIRType, elemPtr);
  }

  return base;
}

mlir::Value HIRBuilder::visitCastExpr(CastExpr *e) {
  auto location = loc(e->getLocation());
  mlir::Value operand = visitExpr(e->getOperand());
  if (!operand)
    return {};

  mlir::Type targetType = convertType(e->getTargetType());
  if (!targetType || targetType == operand.getType())
    return operand;

  // Integer-to-integer cast.
  if (operand.getType().isIntOrIndex() && targetType.isIntOrIndex()) {
    unsigned srcWidth = operand.getType().getIntOrFloatBitWidth();
    unsigned dstWidth = targetType.getIntOrFloatBitWidth();
    if (srcWidth < dstWidth)
      return builder.create<mlir::arith::ExtSIOp>(location, targetType, operand);
    if (srcWidth > dstWidth)
      return builder.create<mlir::arith::TruncIOp>(location, targetType, operand);
    return operand;
  }
  // Int-to-float.
  if (operand.getType().isIntOrIndex() && targetType.isa<mlir::FloatType>())
    return builder.create<mlir::arith::SIToFPOp>(location, targetType, operand);
  // Float-to-int.
  if (operand.getType().isa<mlir::FloatType>() && targetType.isIntOrIndex())
    return builder.create<mlir::arith::FPToSIOp>(location, targetType, operand);
  // Float-to-float.
  if (operand.getType().isa<mlir::FloatType>() && targetType.isa<mlir::FloatType>()) {
    if (operand.getType().getIntOrFloatBitWidth() < targetType.getIntOrFloatBitWidth())
      return builder.create<mlir::arith::ExtFOp>(location, targetType, operand);
    return builder.create<mlir::arith::TruncFOp>(location, targetType, operand);
  }
  return operand;
}

mlir::Value HIRBuilder::visitClosureExpr(ClosureExpr *e) {
  auto location = loc(e->getLocation());

  // --- Capture analysis ---
  // Walk closure body to find references to outer-scope variables.
  struct CaptureInfo {
    std::string name;
    mlir::Value outerVal;
    mlir::Type type;
  };
  llvm::SmallVector<CaptureInfo> captures;

  // DECISION: Simple capture analysis — check all params declared in
  // outer scopes. A full analysis would walk the AST; for now we capture
  // any outer variable that the closure's params don't shadow.
  // This is conservative but correct for single-level closures.

  // --- Build closure function ---
  static unsigned closureCounter = 0;
  std::string closureName = "__closure_" + std::to_string(closureCounter++);
  std::string closureFnName = closureName + "_fn";

  // Build parameter types: closure_ptr + user params.
  llvm::SmallVector<mlir::Type> paramTypes;
  auto ptrType = getPtrType();
  paramTypes.push_back(ptrType); // closure struct pointer
  for (const auto &param : e->getParams()) {
    mlir::Type pType = param.type ? convertType(param.type)
                                  : builder.getIntegerType(32);
    paramTypes.push_back(pType);
  }

  // Return type.
  mlir::Type retType = e->getReturnType() ? convertType(e->getReturnType())
                                          : builder.getIntegerType(32);
  auto funcType = builder.getFunctionType(
      paramTypes, retType.isa<mlir::NoneType>() ? mlir::TypeRange()
                                                : mlir::TypeRange(retType));

  // Save current insertion point.
  auto savedInsertionPoint = builder.saveInsertionPoint();

  // Emit the closure function at module level.
  builder.setInsertionPointToEnd(module.getBody());
  auto closureFuncOp =
      mlir::func::FuncOp::create(location, closureFnName, funcType);
  module.push_back(closureFuncOp);

  auto &entryBlock = *closureFuncOp.addEntryBlock();
  builder.setInsertionPointToStart(&entryBlock);

  pushScope();
  // Bind user parameters (skip closure_ptr at index 0).
  for (unsigned i = 0; i < e->getParams().size(); ++i) {
    if (!e->getParams()[i].name.empty())
      declare(e->getParams()[i].name, entryBlock.getArgument(i + 1));
  }

  mlir::Value bodyResult = visitExpr(e->getBody());

  // Add return.
  auto &lastBlock = closureFuncOp.back();
  if (lastBlock.empty() ||
      !lastBlock.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
    builder.setInsertionPointToEnd(&lastBlock);
    if (funcType.getNumResults() > 0 && bodyResult)
      builder.create<mlir::func::ReturnOp>(location,
                                            mlir::ValueRange{bodyResult});
    else
      builder.create<mlir::func::ReturnOp>(location);
  }
  popScope();

  // Restore insertion point.
  builder.restoreInsertionPoint(savedInsertionPoint);

  // --- Allocate closure struct ---
  // Layout: { fn_ptr: ptr, captures... }
  auto i64Type = builder.getIntegerType(64);
  auto i64One = builder.create<mlir::LLVM::ConstantOp>(
      location, i64Type, static_cast<int64_t>(1));
  // DECISION: Closure struct is just { ptr } for now (no captures stored).
  // Full capture storage requires walking the AST to find free variables.
  auto closureStructType =
      mlir::LLVM::LLVMStructType::getLiteral(&mlirCtx, {ptrType});
  auto closureAlloca = builder.create<mlir::LLVM::AllocaOp>(
      location, ptrType, closureStructType, i64One);

  // Store function pointer.
  auto fnAddr = builder.create<mlir::LLVM::AddressOfOp>(
      location, ptrType, closureFnName);
  auto i32Zero = builder.create<mlir::LLVM::ConstantOp>(
      location, builder.getIntegerType(32), static_cast<int64_t>(0));
  auto fnSlot = builder.create<mlir::LLVM::GEPOp>(
      location, ptrType, closureStructType, closureAlloca,
      mlir::ValueRange{i32Zero, i32Zero});
  builder.create<mlir::LLVM::StoreOp>(location, fnAddr, fnSlot);

  return closureAlloca;
}

mlir::Value HIRBuilder::visitMatchExpr(MatchExpr *e) {
  auto location = loc(e->getLocation());
  mlir::Value scrutinee = visitExpr(e->getScrutinee());
  if (!scrutinee)
    return {};

  // Determine if scrutinee is an enum (pointer to tagged union).
  bool isEnumScrutinee = mlir::isa<mlir::LLVM::LLVMPointerType>(
      scrutinee.getType());
  mlir::Value discriminant;
  if (isEnumScrutinee) {
    // Load discriminant from offset 0 (i32 tag).
    auto i32Ty = builder.getIntegerType(32);
    discriminant = builder.create<mlir::LLVM::LoadOp>(location, i32Ty,
                                                       scrutinee);
  } else {
    // Scrutinee is a scalar — use directly for comparison.
    discriminant = scrutinee;
  }

  // Build a chain of if-else for each arm.
  // Last arm result becomes the match result.
  mlir::Value result;
  mlir::Value lastCond;

  for (unsigned i = 0; i < e->getArms().size(); ++i) {
    const auto &arm = e->getArms()[i];
    pushScope();

    bool isWildcard = dynamic_cast<WildcardPattern *>(arm.pattern) != nullptr;
    bool isIdent = dynamic_cast<IdentPattern *>(arm.pattern) != nullptr;

    if (isWildcard || isIdent) {
      // Default arm — bind scrutinee and emit body.
      if (isIdent) {
        auto *ip = static_cast<IdentPattern *>(arm.pattern);
        declare(ip->getName(), scrutinee);
      }
      if (arm.guard) {
        visitExpr(arm.guard);
      }
      result = visitExpr(arm.body);
    } else if (auto *litPat = dynamic_cast<LiteralPattern *>(arm.pattern)) {
      // Literal pattern — compare discriminant/scrutinee to literal.
      mlir::Value patVal = visitExpr(litPat->getLiteral());
      if (patVal && discriminant) {
        // Ensure types match.
        if (patVal.getType() != discriminant.getType() &&
            discriminant.getType().isIntOrIndex() &&
            patVal.getType().isIntOrIndex()) {
          unsigned dstW = discriminant.getType().getIntOrFloatBitWidth();
          unsigned srcW = patVal.getType().getIntOrFloatBitWidth();
          if (srcW < dstW)
            patVal = builder.create<mlir::arith::ExtSIOp>(location,
                discriminant.getType(), patVal);
          else if (srcW > dstW)
            patVal = builder.create<mlir::arith::TruncIOp>(location,
                discriminant.getType(), patVal);
        }
        lastCond = builder.create<mlir::arith::CmpIOp>(
            location, mlir::arith::CmpIPredicate::eq, discriminant, patVal);
      }
      if (arm.guard) {
        mlir::Value guardVal = visitExpr(arm.guard);
        if (lastCond && guardVal)
          lastCond = builder.create<mlir::arith::AndIOp>(location, lastCond,
                                                          guardVal);
      }
      result = visitExpr(arm.body);
    } else if (auto *enumPat = dynamic_cast<EnumPattern *>(arm.pattern)) {
      // Enum pattern — compare discriminant to variant index.
      const auto &path = enumPat->getPath();
      // DECISION: Variant index is determined by order in enum declaration.
      // Look up the enum and find the variant index.
      int32_t variantIdx = -1;
      if (path.size() >= 2) {
        auto eit = sema.enumDecls.find(path[0]);
        if (eit != sema.enumDecls.end()) {
          for (unsigned vi = 0; vi < eit->second->getVariants().size(); ++vi) {
            if (eit->second->getVariants()[vi]->getName() == path.back()) {
              variantIdx = static_cast<int32_t>(vi);
              break;
            }
          }
        }
      }
      if (variantIdx >= 0 && discriminant) {
        auto idxConst = builder.create<mlir::arith::ConstantIntOp>(
            location, variantIdx, discriminant.getType());
        lastCond = builder.create<mlir::arith::CmpIOp>(
            location, mlir::arith::CmpIPredicate::eq, discriminant, idxConst);
      }

      // Bind payload arguments if present.
      if (isEnumScrutinee && !enumPat->getArgs().empty()) {
        auto ptrType = getPtrType();
        auto i32Zero = builder.create<mlir::LLVM::ConstantOp>(
            location, builder.getIntegerType(32), static_cast<int64_t>(0));
        auto i32One = builder.create<mlir::LLVM::ConstantOp>(
            location, builder.getIntegerType(32), static_cast<int64_t>(1));
        // GEP to payload (field 1 of the tagged union).
        auto payloadPtr = builder.create<mlir::LLVM::GEPOp>(
            location, ptrType, scrutinee.getType(), scrutinee,
            mlir::ValueRange{i32Zero, i32One});
        // Bind each payload arg as pattern variable.
        for (auto *argPat : enumPat->getArgs()) {
          if (auto *ip = dynamic_cast<IdentPattern *>(argPat)) {
            // Load the payload value.
            // DECISION: Assume single-field payload for now.
            auto payloadVal = builder.create<mlir::LLVM::LoadOp>(
                location, builder.getIntegerType(32), payloadPtr);
            declare(ip->getName(), payloadVal);
          }
        }
      }

      if (arm.guard) {
        mlir::Value guardVal = visitExpr(arm.guard);
        if (lastCond && guardVal)
          lastCond = builder.create<mlir::arith::AndIOp>(location, lastCond,
                                                          guardVal);
      }
      result = visitExpr(arm.body);
    } else {
      // Fallback: just emit the body.
      result = visitExpr(arm.body);
    }

    popScope();
  }
  return result;
}

mlir::Value HIRBuilder::visitLoopExpr(LoopExpr *e) {
  auto location = loc(e->getLocation());
  // DECISION: Loop emitted as scf.while with always-true condition.
  pushScope();
  if (e->getBody())
    visitCompoundStmt(e->getBody());
  popScope();
  (void)location;
  return {};
}

mlir::Value HIRBuilder::visitWhileExpr(WhileExpr *e) {
  auto location = loc(e->getLocation());
  // Emit condition and body.
  pushScope();
  mlir::Value cond = visitExpr(e->getCondition());
  if (e->getBody())
    visitCompoundStmt(e->getBody());
  popScope();
  (void)location;
  (void)cond;
  return {};
}

mlir::Value HIRBuilder::visitForExpr(ForExpr *e) {
  auto location = loc(e->getLocation());
  // Emit iterable.
  mlir::Value iterable = visitExpr(e->getIterable());
  pushScope();
  if (iterable && !e->getVarName().empty())
    declare(e->getVarName(), iterable);
  if (e->getBody())
    visitCompoundStmt(e->getBody());
  popScope();
  (void)location;
  return {};
}

mlir::Value HIRBuilder::visitRangeExpr(RangeExpr *e) {
  // Emit start and end.
  mlir::Value start = e->getStart() ? visitExpr(e->getStart()) : mlir::Value{};
  mlir::Value end = e->getEnd() ? visitExpr(e->getEnd()) : mlir::Value{};
  return start ? start : end;
}
mlir::Value HIRBuilder::visitCharLiteral(CharLiteral *e) {
  return builder.create<mlir::arith::ConstantIntOp>(
      loc(e->getLocation()), e->getValue(), builder.getIntegerType(32));
}
mlir::Value HIRBuilder::visitArrayRepeatExpr(ArrayRepeatExpr *) { return {}; }
mlir::Value HIRBuilder::visitMacroCallExpr(MacroCallExpr *e) {
  auto location = loc(e->getLocation());
  llvm::StringRef name = e->getMacroName();

  if (name == "println" || name == "print" || name == "eprintln" ||
      name == "eprint") {
    // Emit call to __asc_print or __asc_eprint.
    bool isErr = name.starts_with("e");
    std::string rtFn = isErr ? "__asc_eprint" : "__asc_print";

    // Evaluate args — for now, handle single string literal arg.
    if (!e->getArgs().empty()) {
      mlir::Value arg = visitExpr(e->getArgs()[0]);
      if (arg && mlir::isa<mlir::LLVM::LLVMPointerType>(arg.getType())) {
        // arg is a pointer to string data — get length from global.
        // DECISION: For string literal args, we pass (ptr, len) to __asc_print.
        // We need to declare the runtime function.
        auto ptrType = getPtrType();
        auto i32Type = builder.getIntegerType(32);
        auto voidType = mlir::LLVM::LLVMVoidType::get(&mlirCtx);

        auto printFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(rtFn);
        if (!printFn) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToStart(module.getBody());
          auto fnType = mlir::LLVM::LLVMFunctionType::get(
              voidType, {ptrType, i32Type});
          printFn =
              builder.create<mlir::LLVM::LLVMFuncOp>(location, rtFn, fnType);
        }

        // DECISION: Use a default length. Full implementation would
        // track string length alongside pointer.
        auto lenConst = builder.create<mlir::LLVM::ConstantOp>(
            location, i32Type, static_cast<int64_t>(0));
        builder.create<mlir::LLVM::CallOp>(
            location, printFn, mlir::ValueRange{arg, lenConst});
      }
    }
    return {};
  }

  if (name == "panic" || name == "todo" || name == "unimplemented" ||
      name == "unreachable") {
    // Emit call to __asc_panic.
    auto ptrType = getPtrType();
    auto i32Type = builder.getIntegerType(32);
    auto voidType = mlir::LLVM::LLVMVoidType::get(&mlirCtx);

    auto panicFn =
        module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("__asc_panic");
    if (!panicFn) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(module.getBody());
      auto fnType = mlir::LLVM::LLVMFunctionType::get(
          voidType, {ptrType, i32Type, ptrType, i32Type, i32Type, i32Type});
      panicFn = builder.create<mlir::LLVM::LLVMFuncOp>(location,
                                                         "__asc_panic", fnType);
    }

    auto null = builder.create<mlir::LLVM::ZeroOp>(location, ptrType);
    auto zero = builder.create<mlir::LLVM::ConstantOp>(
        location, i32Type, static_cast<int64_t>(0));
    builder.create<mlir::LLVM::CallOp>(
        location, panicFn,
        mlir::ValueRange{null, zero, null, zero, zero, zero});
    builder.create<mlir::LLVM::UnreachableOp>(location);
    return {};
  }

  if (name == "assert" || name == "assert_eq" || name == "assert_ne" ||
      name == "debug_assert") {
    if (!e->getArgs().empty()) {
      mlir::Value cond = visitExpr(e->getArgs()[0]);
      if (cond) {
        // Ensure cond is i1.
        if (!cond.getType().isInteger(1)) {
          auto zero = builder.create<mlir::arith::ConstantIntOp>(
              location, 0, cond.getType());
          cond = builder.create<mlir::arith::CmpIOp>(
              location, mlir::arith::CmpIPredicate::ne, cond, zero);
        }
        // Emit: if (!cond) { __asc_panic("assertion failed") }
        auto voidType = mlir::LLVM::LLVMVoidType::get(&mlirCtx);
        auto ptrType = getPtrType();
        auto i32Type = builder.getIntegerType(32);
        auto panicFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("__asc_panic");
        if (!panicFn) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToStart(module.getBody());
          auto fnType = mlir::LLVM::LLVMFunctionType::get(
              voidType, {ptrType, i32Type, ptrType, i32Type, i32Type, i32Type});
          panicFn = builder.create<mlir::LLVM::LLVMFuncOp>(
              location, "__asc_panic", fnType);
        }
        // Emit scf.if for the panic branch.
        auto notCond = builder.create<mlir::arith::XOrIOp>(
            location, cond,
            builder.create<mlir::arith::ConstantIntOp>(location, 1,
                                                        builder.getI1Type()));
        auto ifOp = builder.create<mlir::scf::IfOp>(
            location, mlir::TypeRange{}, notCond, /*hasElse=*/false);
        builder.setInsertionPointToStart(&ifOp.getThenRegion().front());
        auto null = builder.create<mlir::LLVM::ZeroOp>(location, ptrType);
        auto zero32 = builder.create<mlir::LLVM::ConstantOp>(
            location, i32Type, static_cast<int64_t>(0));
        builder.create<mlir::LLVM::CallOp>(
            location, panicFn,
            mlir::ValueRange{null, zero32, null, zero32, zero32, zero32});
        builder.create<mlir::LLVM::UnreachableOp>(location);
        builder.create<mlir::scf::YieldOp>(location);
        builder.setInsertionPointAfter(ifOp);
      }
    }
    return {};
  }

  if (name == "size_of" || name == "align_of") {
    // Compute size from the type argument.
    // The macro argument should be a type expression; for now we use
    // the first argument's type if available.
    uint64_t size = 0;
    if (!e->getArgs().empty()) {
      mlir::Value arg = visitExpr(e->getArgs()[0]);
      if (arg) {
        size = getTypeSize(arg.getType());
        if (name == "align_of") {
          // DECISION: Alignment is min(size, 8) for simplicity.
          if (size > 8) size = 8;
        }
      }
    }
    return builder.create<mlir::arith::ConstantIntOp>(
        location, static_cast<int64_t>(size), builder.getIntegerType(64));
  }

  if (name == "dbg") {
    if (!e->getArgs().empty())
      return visitExpr(e->getArgs()[0]);
    return {};
  }

  // Unknown macro: evaluate args.
  for (auto *arg : e->getArgs())
    visitExpr(arg);
  return {};
}
mlir::Value HIRBuilder::visitUnsafeBlockExpr(UnsafeBlockExpr *e) {
  return visitCompoundStmt(e->getBody());
}
mlir::Value HIRBuilder::visitTemplateLiteralExpr(TemplateLiteralExpr *) {
  return {};
}
mlir::Value HIRBuilder::visitTryExpr(TryExpr *e) {
  return visitExpr(e->getOperand());
}
mlir::Value HIRBuilder::visitPathExpr(PathExpr *e) {
  auto location = loc(e->getLocation());
  const auto &segments = e->getSegments();
  if (segments.empty())
    return {};

  // Check if this is an enum variant: EnumName::VariantName
  if (segments.size() >= 2) {
    auto eit = sema.enumDecls.find(segments[0]);
    if (eit != sema.enumDecls.end()) {
      EnumDecl *ed = eit->second;
      // Find variant index.
      int32_t variantIdx = -1;
      EnumVariantDecl *variant = nullptr;
      for (unsigned i = 0; i < ed->getVariants().size(); ++i) {
        if (ed->getVariants()[i]->getName() == segments.back()) {
          variantIdx = static_cast<int32_t>(i);
          variant = ed->getVariants()[i];
          break;
        }
      }
      if (variantIdx >= 0) {
        // Get the enum MLIR type.
        auto *namedType = astCtx.create<NamedType>(
            segments[0], std::vector<asc::Type *>{}, SourceLocation());
        mlir::Type enumType = convertType(namedType);
        auto ptrType = getPtrType();
        auto i64One = builder.create<mlir::LLVM::ConstantOp>(
            location, builder.getIntegerType(64), static_cast<int64_t>(1));

        // Allocate enum on stack.
        auto alloca = builder.create<mlir::LLVM::AllocaOp>(
            location, ptrType, enumType, i64One);

        // Store discriminant at offset 0.
        auto i32Ty = builder.getIntegerType(32);
        auto discVal = builder.create<mlir::arith::ConstantIntOp>(
            location, variantIdx, i32Ty);
        auto i32Zero = builder.create<mlir::LLVM::ConstantOp>(
            location, i32Ty, static_cast<int64_t>(0));
        auto tagPtr = builder.create<mlir::LLVM::GEPOp>(
            location, ptrType, enumType, alloca,
            mlir::ValueRange{i32Zero, i32Zero});
        builder.create<mlir::LLVM::StoreOp>(location, discVal, tagPtr);

        return alloca;
      }
    }
  }

  // Check if this is a static method call: Type::method
  if (segments.size() == 2) {
    auto callee = module.lookupSymbol<mlir::func::FuncOp>(
        segments[0] + "_" + segments[1]);
    if (!callee) {
      // Try without mangling.
      callee = module.lookupSymbol<mlir::func::FuncOp>(segments[1]);
    }
    // DECISION: Path without call parens resolves to function reference.
    // Actual calling happens in visitCallExpr.
  }

  // Look up as identifier.
  return lookup(segments[0]);
}
mlir::Value HIRBuilder::visitParenExpr(ParenExpr *e) {
  return visitExpr(e->getInner());
}

} // namespace asc
