#include "asc/Sema/Sema.h"

namespace asc {

Type *Sema::checkExpr(Expr *e) {
  if (!e)
    return nullptr;

  Type *result = nullptr;

  switch (e->getKind()) {
  case ExprKind::IntegerLiteral:
    result = checkIntegerLiteral(static_cast<IntegerLiteral *>(e));
    break;
  case ExprKind::FloatLiteral:
    result = checkFloatLiteral(static_cast<FloatLiteral *>(e));
    break;
  case ExprKind::StringLiteral:
    result = checkStringLiteral(static_cast<StringLiteral *>(e));
    break;
  case ExprKind::BoolLiteral:
    result = checkBoolLiteral(static_cast<BoolLiteral *>(e));
    break;
  case ExprKind::NullLiteral:
    result = nullptr; // null has no concrete type yet
    break;
  case ExprKind::DeclRef:
    result = checkDeclRefExpr(static_cast<DeclRefExpr *>(e));
    break;
  case ExprKind::Binary:
    result = checkBinaryExpr(static_cast<BinaryExpr *>(e));
    break;
  case ExprKind::Unary:
    result = checkUnaryExpr(static_cast<UnaryExpr *>(e));
    break;
  case ExprKind::Call:
    result = checkCallExpr(static_cast<CallExpr *>(e));
    break;
  case ExprKind::If:
    result = checkIfExpr(static_cast<IfExpr *>(e));
    break;
  case ExprKind::Block:
    result = checkBlockExpr(static_cast<BlockExpr *>(e));
    break;
  case ExprKind::Assign:
    result = checkAssignExpr(static_cast<AssignExpr *>(e));
    break;
  case ExprKind::CharLiteral:
    result = ctx.getBuiltinType(BuiltinTypeKind::Char);
    break;
  case ExprKind::Paren: {
    auto *pe = static_cast<ParenExpr *>(e);
    result = checkExpr(pe->getInner());
    break;
  }
  default:
    // For expressions not yet handled, return nullptr (type unknown).
    break;
  }

  if (result)
    e->setType(result);
  return result;
}

Type *Sema::checkIntegerLiteral(IntegerLiteral *e) {
  llvm::StringRef suffix = e->getSuffix();
  if (suffix.empty())
    return ctx.getBuiltinType(BuiltinTypeKind::I32); // default

  auto btk = llvm::StringSwitch<int>(suffix)
                 .Case("i8", (int)BuiltinTypeKind::I8)
                 .Case("i16", (int)BuiltinTypeKind::I16)
                 .Case("i32", (int)BuiltinTypeKind::I32)
                 .Case("i64", (int)BuiltinTypeKind::I64)
                 .Case("i128", (int)BuiltinTypeKind::I128)
                 .Case("u8", (int)BuiltinTypeKind::U8)
                 .Case("u16", (int)BuiltinTypeKind::U16)
                 .Case("u32", (int)BuiltinTypeKind::U32)
                 .Case("u64", (int)BuiltinTypeKind::U64)
                 .Case("u128", (int)BuiltinTypeKind::U128)
                 .Case("usize", (int)BuiltinTypeKind::USize)
                 .Case("isize", (int)BuiltinTypeKind::ISize)
                 .Default(-1);
  if (btk >= 0)
    return ctx.getBuiltinType(static_cast<BuiltinTypeKind>(btk));
  return ctx.getBuiltinType(BuiltinTypeKind::I32);
}

Type *Sema::checkFloatLiteral(FloatLiteral *e) {
  llvm::StringRef suffix = e->getSuffix();
  if (suffix == "f32")
    return ctx.getBuiltinType(BuiltinTypeKind::F32);
  return ctx.getBuiltinType(BuiltinTypeKind::F64); // default
}

Type *Sema::checkStringLiteral(StringLiteral *) {
  // DECISION: String literals are ref<str> (static). Represented as a named
  // type for now; the actual str type will be defined in std.
  return ctx.create<NamedType>("str", std::vector<Type *>{}, SourceLocation());
}

Type *Sema::checkBoolLiteral(BoolLiteral *) {
  return ctx.getBuiltinType(BuiltinTypeKind::Bool);
}

Type *Sema::checkDeclRefExpr(DeclRefExpr *e) {
  Symbol *sym = currentScope->lookup(e->getName());
  if (!sym) {
    diags.emitError(e->getLocation(), DiagID::ErrUndeclaredIdentifier,
                    "undeclared identifier '" + e->getName().str() + "'");
    return nullptr;
  }
  e->setResolvedDecl(sym->decl);
  return sym->type;
}

Type *Sema::checkBinaryExpr(BinaryExpr *e) {
  Type *lhsType = checkExpr(e->getLHS());
  Type *rhsType = checkExpr(e->getRHS());

  // Comparison operators always return bool.
  switch (e->getOp()) {
  case BinaryOp::Eq:
  case BinaryOp::Ne:
  case BinaryOp::Lt:
  case BinaryOp::Gt:
  case BinaryOp::Le:
  case BinaryOp::Ge:
    return ctx.getBuiltinType(BuiltinTypeKind::Bool);
  case BinaryOp::LogAnd:
  case BinaryOp::LogOr:
    return ctx.getBuiltinType(BuiltinTypeKind::Bool);
  default:
    break;
  }

  // Arithmetic/bitwise: result type is LHS type (after promotion).
  if (lhsType && rhsType && !isCompatible(lhsType, rhsType)) {
    diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                    "binary operator type mismatch");
  }
  return lhsType;
}

Type *Sema::checkUnaryExpr(UnaryExpr *e) {
  Type *opType = checkExpr(e->getOperand());
  switch (e->getOp()) {
  case UnaryOp::Not:
    return ctx.getBuiltinType(BuiltinTypeKind::Bool);
  case UnaryOp::Neg:
  case UnaryOp::BitNot:
    return opType;
  case UnaryOp::AddrOf:
    if (opType)
      return ctx.create<RefType>(opType, e->getLocation());
    return nullptr;
  case UnaryOp::Deref:
    if (auto *ref = dynamic_cast<RefType *>(opType))
      return ref->getInner();
    if (auto *refmut = dynamic_cast<RefMutType *>(opType))
      return refmut->getInner();
    return opType;
  }
  return opType;
}

Type *Sema::checkCallExpr(CallExpr *e) {
  Type *calleeType = checkExpr(e->getCallee());
  for (auto *arg : e->getArgs())
    checkExpr(arg);

  // If callee is a function, return its return type.
  if (auto *declRef = dynamic_cast<DeclRefExpr *>(e->getCallee())) {
    if (auto *fnDecl = dynamic_cast<FunctionDecl *>(declRef->getResolvedDecl())) {
      return fnDecl->getReturnType();
    }
  }
  return nullptr;
}

Type *Sema::checkIfExpr(IfExpr *e) {
  Type *condType = checkExpr(e->getCondition());
  if (condType && !isCompatible(condType, ctx.getBoolType())) {
    diags.emitError(e->getCondition()->getLocation(), DiagID::ErrTypeMismatch,
                    "if condition must be bool");
  }

  pushScope();
  Type *thenType = nullptr;
  if (e->getThenBlock()) {
    checkCompoundStmt(e->getThenBlock());
    if (e->getThenBlock()->getTrailingExpr())
      thenType = checkExpr(e->getThenBlock()->getTrailingExpr());
  }
  popScope();

  if (e->getElseBlock()) {
    pushScope();
    checkStmt(e->getElseBlock());
    popScope();
  }

  return thenType;
}

Type *Sema::checkBlockExpr(BlockExpr *e) {
  if (!e->getBlock())
    return ctx.getVoidType();
  pushScope();
  for (auto *stmt : e->getBlock()->getStmts())
    checkStmt(stmt);
  Type *result = nullptr;
  if (e->getBlock()->getTrailingExpr())
    result = checkExpr(e->getBlock()->getTrailingExpr());
  popScope();
  return result ? result : ctx.getVoidType();
}

Type *Sema::checkAssignExpr(AssignExpr *e) {
  Type *targetType = checkExpr(e->getTarget());
  Type *valueType = checkExpr(e->getValue());

  // Check mutability.
  if (auto *ref = dynamic_cast<DeclRefExpr *>(e->getTarget())) {
    Symbol *sym = currentScope->lookup(ref->getName());
    if (sym && !sym->isMutable) {
      diags.emitError(e->getLocation(), DiagID::ErrInvalidAssignTarget,
                      "cannot assign to immutable variable '" +
                      ref->getName().str() + "'");
    }
  }

  if (targetType && valueType && !isCompatible(targetType, valueType)) {
    diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                    "assignment type mismatch");
  }

  return ctx.getVoidType();
}

} // namespace asc
