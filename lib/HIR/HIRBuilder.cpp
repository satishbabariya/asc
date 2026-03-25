#include "asc/HIR/HIRBuilder.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinTypes.h"

namespace asc {

HIRBuilder::HIRBuilder(mlir::MLIRContext &mlirCtx, ASTContext &astCtx,
                       Sema &sema)
    : mlirCtx(mlirCtx), astCtx(astCtx), sema(sema), builder(&mlirCtx) {
  // Register dialects.
  mlirCtx.loadDialect<own::OwnDialect>();
  mlirCtx.loadDialect<task::TaskDialect>();
  mlirCtx.loadDialect<mlir::arith::ArithDialect>();
  mlirCtx.loadDialect<mlir::func::FuncDialect>();
  mlirCtx.loadDialect<mlir::scf::SCFDialect>();
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

  // Named types: default to i64 placeholder.
  // DECISION: Full type resolution deferred to codegen.
  return builder.getIntegerType(64);
}

mlir::Location HIRBuilder::loc(SourceLocation astLoc) {
  // DECISION: Use unknown loc for now; full source loc integration deferred.
  return builder.getUnknownLoc();
}

bool HIRBuilder::isOwnedType(asc::Type *astType) {
  return dynamic_cast<OwnType *>(astType) != nullptr;
}

mlir::Value HIRBuilder::emitAlloc(mlir::Type type, mlir::Value init,
                                   mlir::Location location) {
  auto ownType = own::OwnValType::get(&mlirCtx, type);
  return builder.create<own::OwnAllocOp>(location, ownType, init);
}

mlir::Value HIRBuilder::emitMove(mlir::Value source, mlir::Location location) {
  return builder.create<own::OwnMoveOp>(location, source);
}

void HIRBuilder::emitDrop(mlir::Value value, mlir::Location location) {
  builder.create<own::OwnDropOp>(location, value);
}

mlir::Value HIRBuilder::emitBorrowRef(mlir::Value owned,
                                       mlir::Location location) {
  return builder.create<own::BorrowRefOp>(location, owned);
}

mlir::Value HIRBuilder::emitBorrowMut(mlir::Value owned,
                                       mlir::Location location) {
  return builder.create<own::BorrowMutOp>(location, owned);
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
  visitCompoundStmt(d->getBody());

  // If function returns void and block has no terminator, add return.
  auto &lastBlock = funcOp.back();
  if (lastBlock.empty() || !lastBlock.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
    builder.setInsertionPointToEnd(&lastBlock);
    builder.create<mlir::func::ReturnOp>(loc(d->getLocation()));
  }

  popScope();

  // Reset insertion point to module body.
  builder.setInsertionPointToEnd(module.getBody());
}

mlir::Value HIRBuilder::visitStructDecl(StructDecl *) { return {}; }
mlir::Value HIRBuilder::visitVarDecl(VarDecl *d) {
  mlir::Value init;
  if (d->getInit())
    init = visitExpr(d->getInit());
  if (init && !d->getName().empty())
    declare(d->getName(), init);
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

mlir::Value HIRBuilder::visitStringLiteral(StringLiteral *) {
  // DECISION: String literals not lowered to MLIR yet; placeholder.
  return {};
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
        calleeName += "::";
      calleeName += seg;
    }
  }

  // Emit arguments.
  llvm::SmallVector<mlir::Value> args;
  for (auto *arg : e->getArgs()) {
    mlir::Value v = visitExpr(arg);
    if (v)
      args.push_back(v);
  }

  // Look up function in module.
  auto callee =
      module.lookupSymbol<mlir::func::FuncOp>(calleeName);
  if (callee) {
    auto callOp = builder.create<mlir::func::CallOp>(location, callee, args);
    return callOp.getNumResults() > 0 ? callOp.getResult(0) : mlir::Value{};
  }

  // External call — create declaration.
  // DECISION: Forward declaration with matching types created on-demand.
  return {};
}

mlir::Value HIRBuilder::visitIfExpr(IfExpr *e) {
  // DECISION: Full if lowering to scf.if deferred.
  if (e->getCondition())
    visitExpr(e->getCondition());
  if (e->getThenBlock())
    visitCompoundStmt(e->getThenBlock());
  if (e->getElseBlock())
    visitStmt(e->getElseBlock());
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

// Remaining visitor stubs.
mlir::Value HIRBuilder::visitArrayLiteral(ArrayLiteral *) { return {}; }
mlir::Value HIRBuilder::visitStructLiteral(StructLiteral *) { return {}; }
mlir::Value HIRBuilder::visitTupleLiteral(TupleLiteral *) { return {}; }
mlir::Value HIRBuilder::visitMethodCallExpr(MethodCallExpr *) { return {}; }
mlir::Value HIRBuilder::visitFieldAccessExpr(FieldAccessExpr *) { return {}; }
mlir::Value HIRBuilder::visitIndexExpr(IndexExpr *) { return {}; }
mlir::Value HIRBuilder::visitCastExpr(CastExpr *e) {
  return visitExpr(e->getOperand());
}
mlir::Value HIRBuilder::visitClosureExpr(ClosureExpr *) { return {}; }
mlir::Value HIRBuilder::visitMatchExpr(MatchExpr *) { return {}; }
mlir::Value HIRBuilder::visitLoopExpr(LoopExpr *) { return {}; }
mlir::Value HIRBuilder::visitWhileExpr(WhileExpr *) { return {}; }
mlir::Value HIRBuilder::visitForExpr(ForExpr *) { return {}; }
mlir::Value HIRBuilder::visitRangeExpr(RangeExpr *) { return {}; }
mlir::Value HIRBuilder::visitCharLiteral(CharLiteral *e) {
  return builder.create<mlir::arith::ConstantIntOp>(
      loc(e->getLocation()), e->getValue(), builder.getIntegerType(32));
}
mlir::Value HIRBuilder::visitArrayRepeatExpr(ArrayRepeatExpr *) { return {}; }
mlir::Value HIRBuilder::visitMacroCallExpr(MacroCallExpr *) { return {}; }
mlir::Value HIRBuilder::visitUnsafeBlockExpr(UnsafeBlockExpr *e) {
  return visitCompoundStmt(e->getBody());
}
mlir::Value HIRBuilder::visitTemplateLiteralExpr(TemplateLiteralExpr *) {
  return {};
}
mlir::Value HIRBuilder::visitTryExpr(TryExpr *e) {
  return visitExpr(e->getOperand());
}
mlir::Value HIRBuilder::visitPathExpr(PathExpr *) { return {}; }
mlir::Value HIRBuilder::visitParenExpr(ParenExpr *e) {
  return visitExpr(e->getInner());
}

} // namespace asc
