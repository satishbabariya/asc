#include "asc/Sema/Sema.h"
#include "llvm/ADT/StringSwitch.h"

namespace asc {

Type *Sema::checkExpr(Expr *e) {
  if (!e)
    return nullptr;

  Type *result = nullptr;

  switch (e->getKind()) {
  case ExprKind::IntegerLiteral:
    result = checkIntegerLiteral(static_cast<IntegerLiteral *>(e));
    markExprOwnership(e, OwnershipKind::Copied);
    break;
  case ExprKind::FloatLiteral:
    result = checkFloatLiteral(static_cast<FloatLiteral *>(e));
    markExprOwnership(e, OwnershipKind::Copied);
    break;
  case ExprKind::StringLiteral:
    result = checkStringLiteral(static_cast<StringLiteral *>(e));
    markExprOwnership(e, OwnershipKind::Borrowed);
    break;
  case ExprKind::BoolLiteral:
    result = checkBoolLiteral(static_cast<BoolLiteral *>(e));
    markExprOwnership(e, OwnershipKind::Copied);
    break;
  case ExprKind::NullLiteral:
    result = nullptr;
    markExprOwnership(e, OwnershipKind::Copied);
    break;
  case ExprKind::CharLiteral:
    result = ctx.getBuiltinType(BuiltinTypeKind::Char);
    markExprOwnership(e, OwnershipKind::Copied);
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
  case ExprKind::MethodCall:
    result = checkMethodCallExpr(static_cast<MethodCallExpr *>(e));
    break;
  case ExprKind::FieldAccess:
    result = checkFieldAccessExpr(static_cast<FieldAccessExpr *>(e));
    break;
  case ExprKind::Index:
    result = checkIndexExpr(static_cast<IndexExpr *>(e));
    break;
  case ExprKind::If:
    result = checkIfExpr(static_cast<IfExpr *>(e));
    break;
  case ExprKind::Match:
    result = checkMatchExpr(static_cast<MatchExpr *>(e));
    break;
  case ExprKind::Loop:
    result = checkLoopExpr(static_cast<LoopExpr *>(e));
    break;
  case ExprKind::While:
    result = checkWhileExpr(static_cast<WhileExpr *>(e));
    break;
  case ExprKind::For:
    result = checkForExpr(static_cast<ForExpr *>(e));
    break;
  case ExprKind::Closure:
    result = checkClosureExpr(static_cast<ClosureExpr *>(e));
    break;
  case ExprKind::Block:
    result = checkBlockExpr(static_cast<BlockExpr *>(e));
    break;
  case ExprKind::Assign:
    result = checkAssignExpr(static_cast<AssignExpr *>(e));
    break;
  case ExprKind::Cast:
    result = checkCastExpr(static_cast<CastExpr *>(e));
    break;
  case ExprKind::Range:
    result = checkRangeExpr(static_cast<RangeExpr *>(e));
    break;
  case ExprKind::StructLiteral:
    result = checkStructLiteral(static_cast<StructLiteral *>(e));
    break;
  case ExprKind::TupleLiteral:
    result = checkTupleLiteral(static_cast<TupleLiteral *>(e));
    break;
  case ExprKind::ArrayLiteral:
    result = checkArrayLiteral(static_cast<ArrayLiteral *>(e));
    break;
  case ExprKind::ArrayRepeat:
    result = checkArrayRepeatExpr(static_cast<ArrayRepeatExpr *>(e));
    break;
  case ExprKind::MacroCall:
    result = checkMacroCallExpr(static_cast<MacroCallExpr *>(e));
    break;
  case ExprKind::Try:
    result = checkTryExpr(static_cast<TryExpr *>(e));
    break;
  case ExprKind::Path:
    result = checkPathExpr(static_cast<PathExpr *>(e));
    break;
  case ExprKind::TemplateLiteral:
    result = checkTemplateLiteralExpr(static_cast<TemplateLiteralExpr *>(e));
    break;
  case ExprKind::UnsafeBlock: {
    auto *ub = static_cast<UnsafeBlockExpr *>(e);
    if (ub->getBody()) {
      pushScope();
      for (auto *s : ub->getBody()->getStmts())
        checkStmt(s);
      if (ub->getBody()->getTrailingExpr())
        result = checkExpr(ub->getBody()->getTrailingExpr());
      popScope();
    }
    break;
  }
  case ExprKind::Paren: {
    auto *pe = static_cast<ParenExpr *>(e);
    result = checkExpr(pe->getInner());
    break;
  }
  }

  if (result)
    e->setType(result);
  return result;
}

// --- Ownership inference helpers ---

void Sema::markExprOwnership(Expr *e, OwnershipKind kind) {
  OwnershipInfo info;
  info.kind = kind;
  Type *t = e->getType();
  if (t) {
    info.isCopy = isCopyType(t);
    info.isSend = isSendType(t);
    info.isSync = isSyncType(t);
  }
  exprOwnership[e] = info;
}

OwnershipKind Sema::inferParamOwnership(Type *paramType) {
  if (!paramType)
    return OwnershipKind::Borrowed;
  if (dynamic_cast<OwnType *>(paramType))
    return OwnershipKind::Moved;
  if (dynamic_cast<RefType *>(paramType))
    return OwnershipKind::Borrowed;
  if (dynamic_cast<RefMutType *>(paramType))
    return OwnershipKind::BorrowedMut;
  // For bare types (no ownership wrapper), infer based on copy-ability.
  if (isCopyType(paramType))
    return OwnershipKind::Copied;
  // DECISION: Unannotated non-copy parameters default to borrowed (RFC-0002 rule 2).
  return OwnershipKind::Borrowed;
}

void Sema::inferCallOwnership(CallExpr *e, FunctionDecl *callee) {
  if (!callee)
    return;
  const auto &params = callee->getParams();
  const auto &args = e->getArgs();

  for (unsigned i = 0; i < args.size() && i < params.size(); ++i) {
    OwnershipKind kind = inferParamOwnership(params[i].type);
    markExprOwnership(args[i], kind);
  }
}

// --- Literal checkers ---

Type *Sema::checkIntegerLiteral(IntegerLiteral *e) {
  llvm::StringRef suffix = e->getSuffix();
  if (suffix.empty())
    return ctx.getBuiltinType(BuiltinTypeKind::I32);

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
  return ctx.getBuiltinType(BuiltinTypeKind::F64);
}

Type *Sema::checkStringLiteral(StringLiteral *) {
  return ctx.create<NamedType>("str", std::vector<Type *>{}, SourceLocation());
}

Type *Sema::checkBoolLiteral(BoolLiteral *) {
  return ctx.getBuiltinType(BuiltinTypeKind::Bool);
}

// --- Reference and operation checkers ---

Type *Sema::checkDeclRefExpr(DeclRefExpr *e) {
  Symbol *sym = currentScope->lookup(e->getName());
  if (!sym) {
    diags.emitError(e->getLocation(), DiagID::ErrUndeclaredIdentifier,
                    "undeclared identifier '" + e->getName().str() + "'");
    return nullptr;
  }
  // Use-after-move detection: check if the value has been moved.
  if (sym->isMoved) {
    diags.emitError(e->getLocation(), DiagID::ErrUseAfterMove,
                    "use of moved value '" + e->getName().str() + "'");
    return sym->type;
  }
  e->setResolvedDecl(sym->decl);
  // Propagate ownership from the variable's symbol.
  if (sym->ownership.kind != OwnershipKind::Unknown)
    exprOwnership[e] = sym->ownership;
  else if (sym->type && isCopyType(sym->type))
    markExprOwnership(e, OwnershipKind::Copied);
  else
    markExprOwnership(e, OwnershipKind::Owned);
  return sym->type;
}

Type *Sema::checkBinaryExpr(BinaryExpr *e) {
  Type *lhsType = checkExpr(e->getLHS());
  Type *rhsType = checkExpr(e->getRHS());

  switch (e->getOp()) {
  case BinaryOp::Eq: case BinaryOp::Ne:
  case BinaryOp::Lt: case BinaryOp::Gt:
  case BinaryOp::Le: case BinaryOp::Ge:
  case BinaryOp::LogAnd: case BinaryOp::LogOr:
    markExprOwnership(e, OwnershipKind::Copied);
    return ctx.getBuiltinType(BuiltinTypeKind::Bool);
  default:
    break;
  }

  if (lhsType && rhsType && !isCompatible(lhsType, rhsType)) {
    diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                    "binary operator type mismatch");
  }
  markExprOwnership(e, OwnershipKind::Copied);
  return lhsType;
}

Type *Sema::checkUnaryExpr(UnaryExpr *e) {
  Type *opType = checkExpr(e->getOperand());
  switch (e->getOp()) {
  case UnaryOp::Not:
    markExprOwnership(e, OwnershipKind::Copied);
    return ctx.getBuiltinType(BuiltinTypeKind::Bool);
  case UnaryOp::Neg:
  case UnaryOp::BitNot:
    markExprOwnership(e, OwnershipKind::Copied);
    return opType;
  case UnaryOp::AddrOf:
    markExprOwnership(e, OwnershipKind::Borrowed);
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
  checkExpr(e->getCallee());
  for (auto *arg : e->getArgs())
    checkExpr(arg);

  FunctionDecl *fnDecl = nullptr;
  if (auto *declRef = dynamic_cast<DeclRefExpr *>(e->getCallee())) {
    fnDecl = dynamic_cast<FunctionDecl *>(declRef->getResolvedDecl());
  }

  // Run ownership inference on arguments.
  if (fnDecl) {
    inferCallOwnership(e, fnDecl);

    // Mark non-copy arguments as moved when passed by value (owned).
    auto &params = fnDecl->getParams();
    auto &args = e->getArgs();
    for (unsigned i = 0; i < args.size() && i < params.size(); ++i) {
      Type *paramType = params[i].type;
      // Skip ref/refmut params — they borrow, not move.
      if (paramType && (dynamic_cast<RefType *>(paramType) ||
                        dynamic_cast<RefMutType *>(paramType)))
        continue;
      // If the argument is a DeclRefExpr to a non-copy variable, mark as moved.
      if (auto *argRef = dynamic_cast<DeclRefExpr *>(args[i])) {
        Type *argType = argRef->getType();
        if (argType && !isCopyType(argType)) {
          Symbol *argSym = currentScope->lookup(argRef->getName());
          if (argSym && !argSym->isMoved) {
            argSym->isMoved = true;
            argSym->movedAt = e->getLocation();
          }
        }
      }
    }

    Type *retType = resolveType(fnDecl->getReturnType());

    // Generic function monomorphization: infer type parameters from arguments.
    if (fnDecl->isGeneric() && !fnDecl->getGenericParams().empty()) {
      // Build type parameter → concrete type map from argument types.
      llvm::StringMap<Type *> typeParamMap;
      auto &gparams = fnDecl->getGenericParams();
      auto &fparams = fnDecl->getParams();
      for (unsigned i = 0; i < args.size() && i < fparams.size(); ++i) {
        Type *paramType = fparams[i].type;
        Type *argType = args[i]->getType();
        if (!paramType || !argType) continue;
        // Check if param type is a type parameter (NamedType matching a generic param).
        if (auto *nt = dynamic_cast<NamedType *>(paramType)) {
          for (auto &gp : gparams) {
            if (gp.name == nt->getName()) {
              typeParamMap[gp.name] = argType;
              break;
            }
          }
        }
        // Check through own<T> wrapper.
        if (auto *ot = dynamic_cast<OwnType *>(paramType)) {
          if (auto *nt = dynamic_cast<NamedType *>(ot->getInner())) {
            for (auto &gp : gparams) {
              if (gp.name == nt->getName()) {
                typeParamMap[gp.name] = argType;
                break;
              }
            }
          }
        }
      }
      // Substitute return type: if return type is a generic parameter, replace it.
      if (retType) {
        if (auto *nt = dynamic_cast<NamedType *>(retType)) {
          auto it = typeParamMap.find(nt->getName());
          if (it != typeParamMap.end())
            retType = it->second;
        }
        if (auto *ot = dynamic_cast<OwnType *>(retType)) {
          if (auto *nt = dynamic_cast<NamedType *>(ot->getInner())) {
            auto it = typeParamMap.find(nt->getName());
            if (it != typeParamMap.end())
              retType = it->second;
          }
        }
      }
    }

    // Return value ownership: if own<T>, mark as Owned; else Copied.
    if (retType && dynamic_cast<OwnType *>(retType))
      markExprOwnership(e, OwnershipKind::Owned);
    else if (retType && isCopyType(retType))
      markExprOwnership(e, OwnershipKind::Copied);
    else
      markExprOwnership(e, OwnershipKind::Owned);
    return retType;
  }
  return nullptr;
}

Type *Sema::checkMethodCallExpr(MethodCallExpr *e) {
  Type *receiverType = checkExpr(e->getReceiver());
  for (auto *arg : e->getArgs())
    checkExpr(arg);

  if (!receiverType)
    return nullptr;

  // Strip ownership wrappers to get the base type name.
  Type *baseType = receiverType;
  if (auto *ot = dynamic_cast<OwnType *>(receiverType))
    baseType = ot->getInner();
  else if (auto *rt = dynamic_cast<RefType *>(receiverType))
    baseType = rt->getInner();
  else if (auto *rmt = dynamic_cast<RefMutType *>(receiverType))
    baseType = rmt->getInner();

  // Look up impl for this type.
  std::string typeName;
  if (auto *nt = dynamic_cast<NamedType *>(baseType))
    typeName = nt->getName().str();

  if (!typeName.empty()) {
    auto it = implDecls.find(typeName);
    if (it != implDecls.end()) {
      for (auto *impl : it->second) {
        for (auto *method : impl->getMethods()) {
          if (method->getName() == e->getMethodName()) {
            return method->getReturnType();
          }
        }
      }
    }
  }

  // dyn Trait method resolution: look up return type from trait declaration.
  if (auto *dt = dynamic_cast<DynTraitType *>(baseType)) {
    if (!dt->getBounds().empty()) {
      auto tit = traitDecls.find(dt->getBounds()[0].name);
      if (tit != traitDecls.end()) {
        for (const auto &item : tit->second->getItems()) {
          if (item.method && item.method->getName() == e->getMethodName()) {
            return item.method->getReturnType();
          }
        }
      }
    }
  }

  // Built-in methods: clone() returns the receiver type.
  if (e->getMethodName() == "clone")
    return baseType;
  // len() returns i32 for strings/vecs.
  if (e->getMethodName() == "len")
    return ctx.getBuiltinType(BuiltinTypeKind::I32);

  // Receiver borrow: method call on ref borrows, on refmut borrows mut.
  markExprOwnership(e->getReceiver(), OwnershipKind::Borrowed);
  return nullptr;
}

Type *Sema::checkFieldAccessExpr(FieldAccessExpr *e) {
  Type *baseType = checkExpr(e->getBase());
  if (!baseType)
    return nullptr;

  // Strip ownership wrappers.
  Type *inner = baseType;
  if (auto *ot = dynamic_cast<OwnType *>(baseType))
    inner = ot->getInner();
  else if (auto *rt = dynamic_cast<RefType *>(baseType))
    inner = rt->getInner();
  else if (auto *rmt = dynamic_cast<RefMutType *>(baseType))
    inner = rmt->getInner();

  if (auto *nt = dynamic_cast<NamedType *>(inner)) {
    auto it = structDecls.find(nt->getName());
    if (it != structDecls.end()) {
      for (auto *field : it->second->getFields()) {
        if (field->getName() == e->getFieldName()) {
          markExprOwnership(e, OwnershipKind::Borrowed);
          return field->getType();
        }
      }
      diags.emitError(e->getLocation(), DiagID::ErrUndeclaredIdentifier,
                      "no field '" + e->getFieldName().str() + "' on struct '" +
                      nt->getName().str() + "'");
    }
  }
  return nullptr;
}

Type *Sema::checkIndexExpr(IndexExpr *e) {
  Type *baseType = checkExpr(e->getBase());
  Type *indexType = checkExpr(e->getIndex());

  if (indexType) {
    auto *bt = dynamic_cast<BuiltinType *>(indexType);
    if (!bt || (!bt->isInteger() && bt->getBuiltinKind() != BuiltinTypeKind::USize)) {
      diags.emitError(e->getIndex()->getLocation(), DiagID::ErrTypeMismatch,
                      "index must be an integer type");
    }
  }

  if (auto *at = dynamic_cast<ArrayType *>(baseType))
    return at->getElementType();
  if (auto *st = dynamic_cast<SliceType *>(baseType))
    return st->getElementType();
  return nullptr;
}

Type *Sema::checkMatchExpr(MatchExpr *e) {
  Type *scrutType = checkExpr(e->getScrutinee());
  Type *armType = nullptr;

  for (const auto &arm : e->getArms()) {
    pushScope();
    // Bind pattern variables.
    if (arm.pattern) {
      if (auto *ip = dynamic_cast<IdentPattern *>(arm.pattern)) {
        Symbol sym;
        sym.name = ip->getName().str();
        sym.type = scrutType;
        currentScope->declare(ip->getName(), std::move(sym));
      }
      // Enum pattern: bind inner pattern variables.
      if (auto *ep = dynamic_cast<EnumPattern *>(arm.pattern)) {
        // Look up the enum variant to get payload types.
        const auto &path = ep->getPath();
        std::vector<Type *> payloadTypes;
        if (path.size() >= 2) {
          auto eit = enumDecls.find(path[0]);
          if (eit != enumDecls.end()) {
            for (auto *v : eit->second->getVariants()) {
              if (v->getName() == path.back()) {
                // Tuple variants: types from tupleTypes
                if (!v->getTupleTypes().empty()) {
                  payloadTypes = std::vector<Type *>(
                      v->getTupleTypes().begin(), v->getTupleTypes().end());
                }
                // Struct variants: types from structFields (by field name)
                if (!v->getStructFields().empty()) {
                  // Map field names to types for named binding.
                  for (unsigned ai = 0; ai < ep->getArgs().size(); ++ai) {
                    if (auto *innerIp = dynamic_cast<IdentPattern *>(ep->getArgs()[ai])) {
                      // Find the struct field matching this pattern name.
                      for (auto *sf : v->getStructFields()) {
                        if (sf->getName() == innerIp->getName()) {
                          payloadTypes.push_back(sf->getType());
                          break;
                        }
                      }
                    }
                  }
                }
                break;
              }
            }
          }
        }
        for (unsigned ai = 0; ai < ep->getArgs().size(); ++ai) {
          if (auto *innerIp = dynamic_cast<IdentPattern *>(ep->getArgs()[ai])) {
            Symbol sym;
            sym.name = innerIp->getName().str();
            sym.type = (ai < payloadTypes.size()) ? payloadTypes[ai] : scrutType;
            currentScope->declare(innerIp->getName(), std::move(sym));
          }
        }
      }
    }
    if (arm.guard)
      checkExpr(arm.guard);

    Type *bodyType = checkExpr(arm.body);
    if (!armType)
      armType = bodyType;
    else if (bodyType && !isCompatible(armType, bodyType)) {
      diags.emitError(arm.loc, DiagID::ErrTypeMismatch,
                      "match arms have incompatible types");
    }
    popScope();
  }
  return armType;
}

Type *Sema::checkForExpr(ForExpr *e) {
  Type *iterableType = checkExpr(e->getIterable());
  pushScope();
  // Bind loop variable with unwrapped element type.
  Symbol sym;
  sym.name = e->getVarName().str();

  // Unwrap known iterable types to their element type.
  Type *elemType = iterableType;
  if (iterableType) {
    if (auto *nt = dynamic_cast<NamedType *>(iterableType)) {
      llvm::StringRef name = nt->getName();
      // Vec<T> → element is T. Monomorphized struct has fields with concrete type.
      if (name.starts_with("Vec")) {
        auto sit = structDecls.find(name);
        if (sit != structDecls.end()) {
          for (auto *field : sit->second->getFields()) {
            if (field->getName() == "ptr" || field->getName() == "data") {
              elemType = field->getType();
              break;
            }
          }
        }
      }
      // String → element is char.
      if (name == "String")
        elemType = ctx.getBuiltinType(BuiltinTypeKind::Char);
    }
    // Array type → element type.
    if (auto *at = dynamic_cast<ArrayType *>(iterableType))
      elemType = at->getElementType();
    // Range expressions keep their type (already the element type).
  }

  sym.type = elemType;
  sym.isMutable = !e->getIsConst();
  if (!e->getVarName().empty())
    currentScope->declare(e->getVarName(), std::move(sym));
  if (e->getBody())
    checkCompoundStmt(e->getBody());
  popScope();
  return ctx.getVoidType();
}

Type *Sema::checkWhileExpr(WhileExpr *e) {
  Type *condType = checkExpr(e->getCondition());
  if (condType && !isCompatible(condType, ctx.getBoolType())) {
    diags.emitError(e->getCondition()->getLocation(), DiagID::ErrTypeMismatch,
                    "while condition must be bool");
  }
  pushScope();
  if (e->getBody())
    checkCompoundStmt(e->getBody());
  popScope();
  return ctx.getVoidType();
}

Type *Sema::checkLoopExpr(LoopExpr *e) {
  pushScope();
  if (e->getBody())
    checkCompoundStmt(e->getBody());
  popScope();
  // DECISION: Loop type is never (unless break with value, tracked elsewhere).
  return ctx.getVoidType();
}

Type *Sema::checkClosureExpr(ClosureExpr *e) {
  pushScope();
  for (const auto &param : e->getParams()) {
    Symbol sym;
    sym.name = param.name;
    sym.type = param.type;
    currentScope->declare(param.name, std::move(sym));
  }
  Type *bodyType = checkExpr(e->getBody());
  popScope();

  // Build function type.
  std::vector<Type *> paramTypes;
  for (const auto &param : e->getParams())
    paramTypes.push_back(param.type);
  Type *retType = e->getReturnType() ? e->getReturnType() : bodyType;
  return ctx.create<FunctionType>(std::move(paramTypes), retType,
                                  e->getLocation());
}

Type *Sema::checkRangeExpr(RangeExpr *e) {
  Type *startType = e->getStart() ? checkExpr(e->getStart()) : nullptr;
  Type *endType = e->getEnd() ? checkExpr(e->getEnd()) : nullptr;
  Type *elemType = startType ? startType : endType;
  // DECISION: Range produces NamedType("Range") with element generic arg.
  std::vector<Type *> args;
  if (elemType)
    args.push_back(elemType);
  return ctx.create<NamedType>("Range", std::move(args), e->getLocation());
}

Type *Sema::checkCastExpr(CastExpr *e) {
  Type *srcType = checkExpr(e->getOperand());
  Type *dstType = e->getTargetType();
  // Allow casts to dyn Trait.
  if (dstType && dynamic_cast<DynTraitType *>(dstType)) {
    markExprOwnership(e, OwnershipKind::Owned);
    return dstType;
  }
  // Validate: only numeric casts allowed otherwise.
  if (srcType && dstType) {
    bool srcNum = dynamic_cast<BuiltinType *>(srcType) != nullptr;
    bool dstNum = dynamic_cast<BuiltinType *>(dstType) != nullptr;
    if (!srcNum || !dstNum) {
      diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                      "'as' cast only valid between numeric types");
    }
  }
  markExprOwnership(e, OwnershipKind::Copied);
  return dstType;
}

Type *Sema::checkStructLiteral(StructLiteral *e) {
  auto it = structDecls.find(e->getTypeName());

  // If not found, try to trigger monomorphization from mangled name.
  // e.g., "Pair_i32_i32" → monomorphize Pair with [i32, i32].
  if (it == structDecls.end()) {
    llvm::StringRef mangledName = e->getTypeName();
    auto underscorePos = mangledName.find('_');
    if (underscorePos != llvm::StringRef::npos) {
      std::string baseName = mangledName.substr(0, underscorePos).str();
      auto baseIt = structDecls.find(baseName);
      if (baseIt != structDecls.end() && !baseIt->second->getGenericParams().empty()) {
        // Parse type args from mangled suffix.
        llvm::StringRef suffix = mangledName.substr(underscorePos + 1);
        std::vector<Type *> typeArgs;
        while (!suffix.empty()) {
          llvm::StringRef part;
          std::tie(part, suffix) = suffix.split('_');
          // Resolve part to a builtin type.
          auto btk = llvm::StringSwitch<int>(part)
            .Case("i8", (int)BuiltinTypeKind::I8)
            .Case("i16", (int)BuiltinTypeKind::I16)
            .Case("i32", (int)BuiltinTypeKind::I32)
            .Case("i64", (int)BuiltinTypeKind::I64)
            .Case("f32", (int)BuiltinTypeKind::F32)
            .Case("f64", (int)BuiltinTypeKind::F64)
            .Case("bool", (int)BuiltinTypeKind::Bool)
            .Default(-1);
          if (btk >= 0) {
            typeArgs.push_back(ctx.getBuiltinType(
                static_cast<BuiltinTypeKind>(btk)));
          }
        }
        if (!typeArgs.empty()) {
          monomorphizeType(baseName, typeArgs);
          it = structDecls.find(e->getTypeName());
        }
      }
    }
  }

  if (it != structDecls.end()) {
    StructDecl *sd = it->second;
    // Check field types.
    for (const auto &fi : e->getFields()) {
      if (fi.value)
        checkExpr(fi.value);
      bool found = false;
      for (auto *field : sd->getFields()) {
        if (field->getName() == fi.name) {
          found = true;
          break;
        }
      }
      if (!found) {
        diags.emitError(fi.loc, DiagID::ErrUndeclaredIdentifier,
                        "no field '" + fi.name + "' on struct '" +
                        e->getTypeName().str() + "'");
      }
    }
    if (e->getSpreadExpr())
      checkExpr(e->getSpreadExpr());
    markExprOwnership(e, OwnershipKind::Owned);
    return ctx.create<NamedType>(e->getTypeName().str(), std::vector<Type *>{},
                                 e->getLocation());
  }

  // Check if it's an enum variant name (struct variant).
  for (auto &[enumName, enumDecl] : enumDecls) {
    for (auto *variant : enumDecl->getVariants()) {
      if (variant->getName() == e->getTypeName()) {
        // Check field types against variant struct fields.
        for (const auto &fi : e->getFields()) {
          if (fi.value)
            checkExpr(fi.value);
        }
        markExprOwnership(e, OwnershipKind::Owned);
        return ctx.create<NamedType>(enumName.str(), std::vector<Type *>{},
                                     e->getLocation());
      }
    }
  }

  diags.emitError(e->getLocation(), DiagID::ErrUndeclaredIdentifier,
                  "unknown struct type '" + e->getTypeName().str() + "'");
  return nullptr;
}

Type *Sema::checkTupleLiteral(TupleLiteral *e) {
  std::vector<Type *> elemTypes;
  for (auto *elem : e->getElements()) {
    Type *t = checkExpr(elem);
    elemTypes.push_back(t);
  }
  return ctx.create<TupleType>(std::move(elemTypes), e->getLocation());
}

Type *Sema::checkArrayLiteral(ArrayLiteral *e) {
  Type *elemType = nullptr;
  for (auto *elem : e->getElements()) {
    Type *t = checkExpr(elem);
    if (!elemType)
      elemType = t;
    else if (t && !isCompatible(elemType, t)) {
      diags.emitError(elem->getLocation(), DiagID::ErrTypeMismatch,
                      "array elements must have the same type");
    }
  }
  uint64_t size = e->getElements().size();
  if (elemType)
    return ctx.create<ArrayType>(elemType, size, e->getLocation());
  return nullptr;
}

Type *Sema::checkArrayRepeatExpr(ArrayRepeatExpr *e) {
  Type *elemType = checkExpr(e->getValue());
  checkExpr(e->getCount());
  if (elemType && !isCopyType(elemType)) {
    diags.emitError(e->getLocation(), DiagID::ErrMissingCopyAttribute,
                    "array repeat element must be @copy");
  }
  return elemType ? ctx.create<ArrayType>(elemType, 0, e->getLocation())
                  : nullptr;
}

Type *Sema::checkMacroCallExpr(MacroCallExpr *e) {
  llvm::StringRef name = e->getMacroName();

  for (auto *arg : e->getArgs())
    checkExpr(arg);

  if (name == "println" || name == "print" || name == "eprintln" ||
      name == "eprint") {
    return ctx.getVoidType();
  }
  if (name == "format") {
    return ctx.create<NamedType>("String", std::vector<Type *>{},
                                 e->getLocation());
  }
  if (name == "panic" || name == "todo" || name == "unimplemented" ||
      name == "unreachable") {
    return ctx.getBuiltinType(BuiltinTypeKind::Never);
  }
  if (name == "assert" || name == "assert_eq" || name == "assert_ne" ||
      name == "debug_assert") {
    return ctx.getVoidType();
  }
  if (name == "dbg") {
    return e->getArgs().empty() ? ctx.getVoidType()
                                : e->getArgs()[0]->getType();
  }
  if (name == "size_of" || name == "align_of") {
    return ctx.getBuiltinType(BuiltinTypeKind::USize);
  }

  // Unknown macro — not an error, may be user-defined.
  return nullptr;
}

Type *Sema::checkTryExpr(TryExpr *e) {
  Type *innerType = checkExpr(e->getOperand());
  if (!innerType)
    return nullptr;

  // Check if the operand type is Result<T,E> or Option<T>.
  if (auto *nt = dynamic_cast<NamedType *>(innerType)) {
    llvm::StringRef name = nt->getName();

    // Result<T,E> → unwrap to T.
    // Monomorphized names look like "Result_i32_String".
    if (name.starts_with("Result")) {
      // The function must return Result to propagate the error.
      if (currentReturnType) {
        if (auto *retNt = dynamic_cast<NamedType *>(currentReturnType)) {
          if (!retNt->getName().starts_with("Result")) {
            diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                            "? operator requires enclosing function to return Result");
          }
        }
      }
      // Look up the Ok variant's type from the enum declaration.
      auto eit = enumDecls.find(name);
      if (eit != enumDecls.end()) {
        for (auto *v : eit->second->getVariants()) {
          if (v->getName() == "Ok" && !v->getTupleTypes().empty())
            return v->getTupleTypes()[0];
        }
      }
      return innerType; // Fallback if we can't resolve the Ok type.
    }

    // Option<T> → unwrap to T.
    if (name.starts_with("Option")) {
      if (currentReturnType) {
        if (auto *retNt = dynamic_cast<NamedType *>(currentReturnType)) {
          if (!retNt->getName().starts_with("Option")) {
            diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                            "? operator requires enclosing function to return Option");
          }
        }
      }
      auto eit = enumDecls.find(name);
      if (eit != enumDecls.end()) {
        for (auto *v : eit->second->getVariants()) {
          if (v->getName() == "Some" && !v->getTupleTypes().empty())
            return v->getTupleTypes()[0];
        }
      }
      return innerType;
    }
  }

  // Neither Result nor Option.
  diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                  "? operator requires Result<T,E> or Option<T> operand");
  return innerType;
}

Type *Sema::checkPathExpr(PathExpr *e) {
  const auto &segments = e->getSegments();
  if (segments.empty())
    return nullptr;

  // Try to resolve as enum variant: EnumName::Variant
  if (segments.size() >= 2) {
    auto it = enumDecls.find(segments[0]);
    if (it != enumDecls.end()) {
      return ctx.create<NamedType>(segments[0], std::vector<Type *>{},
                                   e->getLocation());
    }
  }

  // Try to resolve as static method: Type::method
  if (segments.size() == 2) {
    auto it = implDecls.find(segments[0]);
    if (it != implDecls.end()) {
      for (auto *impl : it->second) {
        for (auto *method : impl->getMethods()) {
          if (method->getName() == segments[1]) {
            return method->getReturnType();
          }
        }
      }
    }
  }

  // Fall back to looking up the first segment as identifier.
  Symbol *sym = currentScope->lookup(segments[0]);
  if (sym)
    return sym->type;

  return nullptr;
}

Type *Sema::checkTemplateLiteralExpr(TemplateLiteralExpr *e) {
  for (const auto &part : e->getParts()) {
    if (part.expr)
      checkExpr(part.expr);
  }
  markExprOwnership(e, OwnershipKind::Owned);
  return ctx.create<NamedType>("String", std::vector<Type *>{},
                               e->getLocation());
}

Type *Sema::checkIfExpr(IfExpr *e) {
  Type *condType = checkExpr(e->getCondition());
  if (condType && !isCompatible(condType, ctx.getBoolType())) {
    diags.emitError(e->getCondition()->getLocation(), DiagID::ErrTypeMismatch,
                    "if condition must be bool");
  }

  // Snapshot moved state before branches for E005 detection.
  llvm::StringMap<bool> moveStateBefore;
  for (Scope *s = currentScope; s; s = s->getParent()) {
    s->forEachSymbol([&](llvm::StringRef name, Symbol &sym) {
      if (!moveStateBefore.count(name))
        moveStateBefore[name] = sym.isMoved;
    });
  }

  if (e->getThenBlock()) {
    checkCompoundStmt(e->getThenBlock());
  }

  // Capture post-then move state.
  llvm::StringMap<bool> moveStateAfterThen;
  for (Scope *s = currentScope; s; s = s->getParent()) {
    s->forEachSymbol([&](llvm::StringRef name, Symbol &sym) {
      if (!moveStateAfterThen.count(name))
        moveStateAfterThen[name] = sym.isMoved;
    });
  }

  if (e->getElseBlock()) {
    // Restore pre-branch move state for the else branch.
    for (Scope *s = currentScope; s; s = s->getParent()) {
      s->forEachSymbol([&](llvm::StringRef name, Symbol &sym) {
        auto it = moveStateBefore.find(name);
        if (it != moveStateBefore.end())
          sym.isMoved = it->second;
      });
    }
    checkStmt(e->getElseBlock());
  }

  // E005: Check for conditional moves — moved in one branch but not the other.
  if (e->getElseBlock()) {
    for (auto &[name, movedAfterThen] : moveStateAfterThen) {
      auto beforeIt = moveStateBefore.find(name);
      if (beforeIt == moveStateBefore.end() || beforeIt->second)
        continue; // already moved before, or not tracked
      Symbol *sym = currentScope->lookup(name);
      if (!sym) continue;
      bool movedAfterElse = sym->isMoved;
      if (movedAfterThen != movedAfterElse) {
        diags.emitWarning(e->getLocation(), DiagID::ErrMoveInConditionalBranch,
                        "value '" + name.str() +
                        "' is moved in one branch of conditional but not the other");
        // Mark as moved on all paths to prevent false use-after-move.
        sym->isMoved = true;
      }
    }
  } else {
    // No else: if then-branch moves something, mark as maybe-moved.
    for (auto &[name, movedAfterThen] : moveStateAfterThen) {
      auto beforeIt = moveStateBefore.find(name);
      if (beforeIt == moveStateBefore.end() || beforeIt->second) continue;
      if (movedAfterThen && !beforeIt->second) {
        Symbol *sym = currentScope->lookup(name);
        if (sym) sym->isMoved = true; // conservatively mark as moved
      }
    }
  }

  return ctx.getVoidType();
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
