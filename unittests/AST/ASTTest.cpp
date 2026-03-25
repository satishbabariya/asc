#include "asc/AST/ASTContext.h"
#include "asc/AST/Decl.h"
#include "asc/AST/Expr.h"
#include "asc/AST/Stmt.h"
#include "asc/AST/Type.h"
#include "gtest/gtest.h"

using namespace asc;

TEST(ASTContextTest, BumpAllocator) {
  ASTContext ctx;
  auto *t1 = ctx.create<BuiltinType>(BuiltinTypeKind::I32, SourceLocation());
  auto *t2 = ctx.create<BuiltinType>(BuiltinTypeKind::F64, SourceLocation());
  EXPECT_NE(t1, t2);
  EXPECT_EQ(t1->getBuiltinKind(), BuiltinTypeKind::I32);
  EXPECT_EQ(t2->getBuiltinKind(), BuiltinTypeKind::F64);
}

TEST(ASTContextTest, CachedBuiltinTypes) {
  ASTContext ctx;
  auto *i32a = ctx.getBuiltinType(BuiltinTypeKind::I32);
  auto *i32b = ctx.getBuiltinType(BuiltinTypeKind::I32);
  EXPECT_EQ(i32a, i32b); // same pointer
  auto *f64 = ctx.getBuiltinType(BuiltinTypeKind::F64);
  EXPECT_NE(i32a, f64);
}

TEST(TypeTest, BuiltinTypeProperties) {
  ASTContext ctx;
  auto *i32 = ctx.getBuiltinType(BuiltinTypeKind::I32);
  EXPECT_TRUE(i32->isInteger());
  EXPECT_TRUE(i32->isSigned());
  EXPECT_FALSE(i32->isFloat());
  EXPECT_FALSE(i32->isUnsigned());

  auto *u64 = ctx.getBuiltinType(BuiltinTypeKind::U64);
  EXPECT_TRUE(u64->isUnsigned());
  EXPECT_FALSE(u64->isSigned());

  auto *f32 = ctx.getBuiltinType(BuiltinTypeKind::F32);
  EXPECT_TRUE(f32->isFloat());
  EXPECT_FALSE(f32->isInteger());

  auto *b = ctx.getBuiltinType(BuiltinTypeKind::Bool);
  EXPECT_TRUE(b->isBool());

  auto *v = ctx.getBuiltinType(BuiltinTypeKind::Void);
  EXPECT_TRUE(v->isVoid());

  auto *n = ctx.getBuiltinType(BuiltinTypeKind::Never);
  EXPECT_TRUE(n->isNever());
}

TEST(TypeTest, OwnershipTypes) {
  ASTContext ctx;
  auto *i32 = ctx.getBuiltinType(BuiltinTypeKind::I32);

  auto *owned = ctx.create<OwnType>(i32, SourceLocation());
  EXPECT_TRUE(owned->isOwn());
  EXPECT_EQ(owned->getInner(), i32);

  auto *ref = ctx.create<RefType>(i32, SourceLocation());
  EXPECT_TRUE(ref->isRef());
  EXPECT_EQ(ref->getInner(), i32);

  auto *refmut = ctx.create<RefMutType>(i32, SourceLocation());
  EXPECT_TRUE(refmut->isRefMut());
  EXPECT_EQ(refmut->getInner(), i32);
}

TEST(TypeTest, NamedType) {
  ASTContext ctx;
  auto *t = ctx.create<NamedType>("Vec", std::vector<Type *>{}, SourceLocation());
  EXPECT_EQ(t->getName(), "Vec");
  EXPECT_TRUE(t->getGenericArgs().empty());
}

TEST(TypeTest, ArrayAndSlice) {
  ASTContext ctx;
  auto *i32 = ctx.getBuiltinType(BuiltinTypeKind::I32);

  auto *arr = ctx.create<ArrayType>(i32, 10, SourceLocation());
  EXPECT_EQ(arr->getElementType(), i32);
  EXPECT_EQ(arr->getSize(), 10u);

  auto *slice = ctx.create<SliceType>(i32, SourceLocation());
  EXPECT_EQ(slice->getElementType(), i32);
}

TEST(TypeTest, TupleType) {
  ASTContext ctx;
  auto *i32 = ctx.getBuiltinType(BuiltinTypeKind::I32);
  auto *f64 = ctx.getBuiltinType(BuiltinTypeKind::F64);

  std::vector<Type *> elems = {i32, f64};
  auto *tuple = ctx.create<TupleType>(elems, SourceLocation());
  EXPECT_EQ(tuple->getElements().size(), 2u);
}

TEST(TypeTest, NullableType) {
  ASTContext ctx;
  auto *i32 = ctx.getBuiltinType(BuiltinTypeKind::I32);
  auto *nullable = ctx.create<NullableType>(i32, SourceLocation());
  EXPECT_EQ(nullable->getInner(), i32);
}

TEST(DeclTest, FunctionDecl) {
  ASTContext ctx;
  auto *body = ctx.create<CompoundStmt>(std::vector<Stmt *>{}, nullptr,
                                        SourceLocation());
  auto *fn = ctx.create<FunctionDecl>(
      "main", std::vector<GenericParam>{}, std::vector<ParamDecl>{},
      ctx.getBuiltinType(BuiltinTypeKind::I32), body,
      std::vector<WhereConstraint>{}, SourceLocation());
  EXPECT_EQ(fn->getName(), "main");
  EXPECT_FALSE(fn->isGeneric());
  EXPECT_NE(fn->getBody(), nullptr);
}

TEST(DeclTest, StructDecl) {
  ASTContext ctx;
  auto *field = ctx.create<FieldDecl>(
      "x", ctx.getBuiltinType(BuiltinTypeKind::F64), SourceLocation());
  auto *sd = ctx.create<StructDecl>(
      "Point", std::vector<GenericParam>{},
      std::vector<FieldDecl *>{field}, SourceLocation());
  EXPECT_EQ(sd->getName(), "Point");
  EXPECT_EQ(sd->getFields().size(), 1u);
  EXPECT_FALSE(sd->isUnit());
}

TEST(DeclTest, UnitStruct) {
  ASTContext ctx;
  auto *sd = ctx.create<StructDecl>(
      "Marker", std::vector<GenericParam>{},
      std::vector<FieldDecl *>{}, SourceLocation());
  EXPECT_TRUE(sd->isUnit());
}

TEST(ExprTest, Literals) {
  ASTContext ctx;
  auto *intLit = ctx.create<IntegerLiteral>(42, "i32", SourceLocation());
  EXPECT_EQ(intLit->getValue(), 42u);
  EXPECT_EQ(intLit->getSuffix(), "i32");

  auto *fltLit = ctx.create<FloatLiteral>(3.14, "f64", SourceLocation());
  EXPECT_DOUBLE_EQ(fltLit->getValue(), 3.14);

  auto *strLit = ctx.create<StringLiteral>("hello", SourceLocation());
  EXPECT_EQ(strLit->getValue(), "hello");

  auto *boolLit = ctx.create<BoolLiteral>(true, SourceLocation());
  EXPECT_TRUE(boolLit->getValue());

  auto *nullLit = ctx.create<NullLiteral>(SourceLocation());
  EXPECT_EQ(nullLit->getKind(), ExprKind::NullLiteral);
}

TEST(ExprTest, BinaryExpr) {
  ASTContext ctx;
  auto *lhs = ctx.create<IntegerLiteral>(1, "", SourceLocation());
  auto *rhs = ctx.create<IntegerLiteral>(2, "", SourceLocation());
  auto *bin = ctx.create<BinaryExpr>(BinaryOp::Add, lhs, rhs, SourceLocation());
  EXPECT_EQ(bin->getOp(), BinaryOp::Add);
  EXPECT_EQ(bin->getLHS(), lhs);
  EXPECT_EQ(bin->getRHS(), rhs);
}

TEST(ExprTest, CallExpr) {
  ASTContext ctx;
  auto *callee = ctx.create<DeclRefExpr>("foo", SourceLocation());
  auto *arg = ctx.create<IntegerLiteral>(42, "", SourceLocation());
  auto *call = ctx.create<CallExpr>(
      callee, std::vector<Expr *>{arg},
      std::vector<Type *>{}, SourceLocation());
  EXPECT_EQ(call->getArgs().size(), 1u);
}

TEST(PatternTest, Patterns) {
  ASTContext ctx;
  auto *wild = ctx.create<WildcardPattern>(SourceLocation());
  EXPECT_EQ(wild->getKind(), PatternKind::Wildcard);

  auto *ident = ctx.create<IdentPattern>("x", false, SourceLocation());
  EXPECT_EQ(ident->getName(), "x");

  auto *lit = ctx.create<IntegerLiteral>(42, "", SourceLocation());
  auto *litPat = ctx.create<LiteralPattern>(lit, SourceLocation());
  EXPECT_EQ(litPat->getLiteral(), lit);
}
