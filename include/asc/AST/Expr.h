#ifndef ASC_AST_EXPR_H
#define ASC_AST_EXPR_H

#include "asc/AST/Type.h"
#include "asc/Basic/SourceLocation.h"
#include "asc/Basic/TokenKinds.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <string>
#include <vector>

namespace asc {

class Stmt;
class CompoundStmt;
class Decl;
class VarDecl;
class Pattern;

/// Discriminator for Expr subclasses.
enum class ExprKind {
  IntegerLiteral,
  FloatLiteral,
  StringLiteral,
  CharLiteral,
  BoolLiteral,
  NullLiteral,
  ArrayLiteral,
  ArrayRepeat,
  StructLiteral,
  TupleLiteral,
  DeclRef,
  Binary,
  Unary,
  Call,
  MethodCall,
  FieldAccess,
  Index,
  Cast,
  Range,
  If,
  IfLet,
  Match,
  Loop,
  While,
  WhileLet,
  For,
  Closure,
  Assign,
  Block,
  MacroCall,
  UnsafeBlock,
  TemplateLiteral,
  Try,
  Path,
  Paren,
};

/// Base class for all expressions.
class Expr {
public:
  virtual ~Expr() = default;
  ExprKind getKind() const { return kind; }
  SourceLocation getLocation() const { return loc; }

  /// The resolved type (set by Sema).
  Type *getType() const { return resolvedType; }
  void setType(Type *t) { resolvedType = t; }

protected:
  Expr(ExprKind kind, SourceLocation loc)
      : kind(kind), loc(loc), resolvedType(nullptr) {}

private:
  ExprKind kind;
  SourceLocation loc;
  Type *resolvedType;
};

// --- Literals ---

class IntegerLiteral : public Expr {
public:
  IntegerLiteral(uint64_t value, std::string suffix, SourceLocation loc)
      : Expr(ExprKind::IntegerLiteral, loc), value(value),
        suffix(std::move(suffix)) {}

  uint64_t getValue() const { return value; }
  llvm::StringRef getSuffix() const { return suffix; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::IntegerLiteral;
  }

private:
  uint64_t value;
  std::string suffix;
};

class FloatLiteral : public Expr {
public:
  FloatLiteral(double value, std::string suffix, SourceLocation loc)
      : Expr(ExprKind::FloatLiteral, loc), value(value),
        suffix(std::move(suffix)) {}

  double getValue() const { return value; }
  llvm::StringRef getSuffix() const { return suffix; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::FloatLiteral;
  }

private:
  double value;
  std::string suffix;
};

class StringLiteral : public Expr {
public:
  StringLiteral(std::string value, SourceLocation loc)
      : Expr(ExprKind::StringLiteral, loc), value(std::move(value)) {}

  llvm::StringRef getValue() const { return value; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::StringLiteral;
  }

private:
  std::string value;
};

class CharLiteral : public Expr {
public:
  CharLiteral(uint32_t value, SourceLocation loc)
      : Expr(ExprKind::CharLiteral, loc), value(value) {}

  uint32_t getValue() const { return value; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::CharLiteral;
  }

private:
  uint32_t value;
};

class BoolLiteral : public Expr {
public:
  BoolLiteral(bool value, SourceLocation loc)
      : Expr(ExprKind::BoolLiteral, loc), value(value) {}

  bool getValue() const { return value; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::BoolLiteral;
  }

private:
  bool value;
};

class NullLiteral : public Expr {
public:
  explicit NullLiteral(SourceLocation loc)
      : Expr(ExprKind::NullLiteral, loc) {}

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::NullLiteral;
  }
};

class ArrayLiteral : public Expr {
public:
  ArrayLiteral(std::vector<Expr *> elements, SourceLocation loc)
      : Expr(ExprKind::ArrayLiteral, loc), elements(std::move(elements)) {}

  const std::vector<Expr *> &getElements() const { return elements; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::ArrayLiteral;
  }

private:
  std::vector<Expr *> elements;
};

/// [expr; count]
class ArrayRepeatExpr : public Expr {
public:
  ArrayRepeatExpr(Expr *value, Expr *count, SourceLocation loc)
      : Expr(ExprKind::ArrayRepeat, loc), value(value), count(count) {}

  Expr *getValue() const { return value; }
  Expr *getCount() const { return count; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::ArrayRepeat;
  }

private:
  Expr *value;
  Expr *count;
};

/// Field initializer in struct literal.
struct FieldInit {
  std::string name;
  Expr *value; // nullptr for shorthand `{ x }` meaning `{ x: x }`
  SourceLocation loc;
};

class StructLiteral : public Expr {
public:
  StructLiteral(std::string typeName, std::vector<FieldInit> fields,
                Expr *spreadExpr, SourceLocation loc)
      : Expr(ExprKind::StructLiteral, loc), typeName(std::move(typeName)),
        fields(std::move(fields)), spreadExpr(spreadExpr) {}

  llvm::StringRef getTypeName() const { return typeName; }
  const std::vector<FieldInit> &getFields() const { return fields; }
  Expr *getSpreadExpr() const { return spreadExpr; } // `..other`

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::StructLiteral;
  }

private:
  std::string typeName;
  std::vector<FieldInit> fields;
  Expr *spreadExpr;
};

class TupleLiteral : public Expr {
public:
  TupleLiteral(std::vector<Expr *> elements, SourceLocation loc)
      : Expr(ExprKind::TupleLiteral, loc), elements(std::move(elements)) {}

  const std::vector<Expr *> &getElements() const { return elements; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::TupleLiteral;
  }

private:
  std::vector<Expr *> elements;
};

// --- References and operations ---

class DeclRefExpr : public Expr {
public:
  DeclRefExpr(std::string name, SourceLocation loc)
      : Expr(ExprKind::DeclRef, loc), name(std::move(name)) {}

  llvm::StringRef getName() const { return name; }

  // Set by Sema.
  Decl *getResolvedDecl() const { return resolvedDecl; }
  void setResolvedDecl(Decl *d) { resolvedDecl = d; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::DeclRef;
  }

private:
  std::string name;
  Decl *resolvedDecl = nullptr;
};

/// Binary operator kind.
enum class BinaryOp {
  Add, Sub, Mul, Div, Mod,
  BitAnd, BitOr, BitXor, Shl, Shr,
  Eq, Ne, Lt, Gt, Le, Ge,
  LogAnd, LogOr,
  Range, RangeInclusive,
};

class BinaryExpr : public Expr {
public:
  BinaryExpr(BinaryOp op, Expr *lhs, Expr *rhs, SourceLocation loc)
      : Expr(ExprKind::Binary, loc), op(op), lhs(lhs), rhs(rhs) {}

  BinaryOp getOp() const { return op; }
  Expr *getLHS() const { return lhs; }
  Expr *getRHS() const { return rhs; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Binary;
  }

private:
  BinaryOp op;
  Expr *lhs;
  Expr *rhs;
};

enum class UnaryOp {
  Neg,     // -
  Not,     // !
  BitNot,  // ~
  AddrOf,  // &
  Deref,   // *
};

class UnaryExpr : public Expr {
public:
  UnaryExpr(UnaryOp op, Expr *operand, SourceLocation loc)
      : Expr(ExprKind::Unary, loc), op(op), operand(operand) {}

  UnaryOp getOp() const { return op; }
  Expr *getOperand() const { return operand; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Unary;
  }

private:
  UnaryOp op;
  Expr *operand;
};

class CallExpr : public Expr {
public:
  CallExpr(Expr *callee, std::vector<Expr *> args,
           std::vector<Type *> genericArgs, SourceLocation loc)
      : Expr(ExprKind::Call, loc), callee(callee), args(std::move(args)),
        genericArgs(std::move(genericArgs)) {}

  Expr *getCallee() const { return callee; }
  const std::vector<Expr *> &getArgs() const { return args; }
  const std::vector<Type *> &getGenericArgs() const { return genericArgs; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Call;
  }

private:
  Expr *callee;
  std::vector<Expr *> args;
  std::vector<Type *> genericArgs;
};

class MethodCallExpr : public Expr {
public:
  MethodCallExpr(Expr *receiver, std::string methodName,
                 std::vector<Expr *> args, std::vector<Type *> genericArgs,
                 SourceLocation loc)
      : Expr(ExprKind::MethodCall, loc), receiver(receiver),
        methodName(std::move(methodName)), args(std::move(args)),
        genericArgs(std::move(genericArgs)) {}

  Expr *getReceiver() const { return receiver; }
  llvm::StringRef getMethodName() const { return methodName; }
  const std::vector<Expr *> &getArgs() const { return args; }
  const std::vector<Type *> &getGenericArgs() const { return genericArgs; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::MethodCall;
  }

private:
  Expr *receiver;
  std::string methodName;
  std::vector<Expr *> args;
  std::vector<Type *> genericArgs;
};

class FieldAccessExpr : public Expr {
public:
  FieldAccessExpr(Expr *base, std::string fieldName, SourceLocation loc)
      : Expr(ExprKind::FieldAccess, loc), base(base),
        fieldName(std::move(fieldName)) {}

  Expr *getBase() const { return base; }
  llvm::StringRef getFieldName() const { return fieldName; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::FieldAccess;
  }

private:
  Expr *base;
  std::string fieldName;
};

class IndexExpr : public Expr {
public:
  IndexExpr(Expr *base, Expr *index, SourceLocation loc)
      : Expr(ExprKind::Index, loc), base(base), index(index) {}

  Expr *getBase() const { return base; }
  Expr *getIndex() const { return index; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Index;
  }

private:
  Expr *base;
  Expr *index;
};

/// `expr as Type`
class CastExpr : public Expr {
public:
  CastExpr(Expr *operand, Type *targetType, SourceLocation loc)
      : Expr(ExprKind::Cast, loc), operand(operand), targetType(targetType) {}

  Expr *getOperand() const { return operand; }
  Type *getTargetType() const { return targetType; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Cast;
  }

private:
  Expr *operand;
  Type *targetType;
};

/// `a..b` or `a..=b`
class RangeExpr : public Expr {
public:
  RangeExpr(Expr *start, Expr *end, bool inclusive, SourceLocation loc)
      : Expr(ExprKind::Range, loc), start(start), end(end),
        inclusive(inclusive) {}

  Expr *getStart() const { return start; }
  Expr *getEnd() const { return end; }
  bool isInclusive() const { return inclusive; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Range;
  }

private:
  Expr *start; // nullable
  Expr *end;   // nullable
  bool inclusive;
};

// --- Control flow expressions ---

class IfExpr : public Expr {
public:
  IfExpr(Expr *condition, CompoundStmt *thenBlock, Stmt *elseBlock,
         SourceLocation loc)
      : Expr(ExprKind::If, loc), condition(condition), thenBlock(thenBlock),
        elseBlock(elseBlock) {}

  Expr *getCondition() const { return condition; }
  CompoundStmt *getThenBlock() const { return thenBlock; }
  Stmt *getElseBlock() const { return elseBlock; } // CompoundStmt or another IfExpr

  static bool classof(const Expr *e) { return e->getKind() == ExprKind::If; }

private:
  Expr *condition;
  CompoundStmt *thenBlock;
  Stmt *elseBlock; // nullable
};

class IfLetExpr : public Expr {
public:
  IfLetExpr(Pattern *pattern, Expr *scrutinee, CompoundStmt *thenBlock,
            Stmt *elseBlock, SourceLocation loc)
      : Expr(ExprKind::IfLet, loc), pattern(pattern), scrutinee(scrutinee),
        thenBlock(thenBlock), elseBlock(elseBlock) {}

  Pattern *getPattern() const { return pattern; }
  Expr *getScrutinee() const { return scrutinee; }
  CompoundStmt *getThenBlock() const { return thenBlock; }
  Stmt *getElseBlock() const { return elseBlock; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::IfLet;
  }

private:
  Pattern *pattern;
  Expr *scrutinee;
  CompoundStmt *thenBlock;
  Stmt *elseBlock; // nullable
};

/// Match arm: `pattern => expr`
struct MatchArm {
  Pattern *pattern;
  Expr *guard; // nullable
  Expr *body;
  SourceLocation loc;
};

class MatchExpr : public Expr {
public:
  MatchExpr(Expr *scrutinee, std::vector<MatchArm> arms, SourceLocation loc)
      : Expr(ExprKind::Match, loc), scrutinee(scrutinee),
        arms(std::move(arms)) {}

  Expr *getScrutinee() const { return scrutinee; }
  const std::vector<MatchArm> &getArms() const { return arms; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Match;
  }

private:
  Expr *scrutinee;
  std::vector<MatchArm> arms;
};

class LoopExpr : public Expr {
public:
  LoopExpr(CompoundStmt *body, std::string label, SourceLocation loc)
      : Expr(ExprKind::Loop, loc), body(body), label(std::move(label)) {}

  CompoundStmt *getBody() const { return body; }
  llvm::StringRef getLabel() const { return label; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Loop;
  }

private:
  CompoundStmt *body;
  std::string label;
};

class WhileExpr : public Expr {
public:
  WhileExpr(Expr *condition, CompoundStmt *body, std::string label,
            SourceLocation loc)
      : Expr(ExprKind::While, loc), condition(condition), body(body),
        label(std::move(label)) {}

  Expr *getCondition() const { return condition; }
  CompoundStmt *getBody() const { return body; }
  llvm::StringRef getLabel() const { return label; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::While;
  }

private:
  Expr *condition;
  CompoundStmt *body;
  std::string label;
};

class WhileLetExpr : public Expr {
public:
  WhileLetExpr(Pattern *pattern, Expr *scrutinee, CompoundStmt *body,
               std::string label, SourceLocation loc)
      : Expr(ExprKind::WhileLet, loc), pattern(pattern), scrutinee(scrutinee),
        body(body), label(std::move(label)) {}

  Pattern *getPattern() const { return pattern; }
  Expr *getScrutinee() const { return scrutinee; }
  CompoundStmt *getBody() const { return body; }
  llvm::StringRef getLabel() const { return label; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::WhileLet;
  }

private:
  Pattern *pattern;
  Expr *scrutinee;
  CompoundStmt *body;
  std::string label;
};

class ForExpr : public Expr {
public:
  ForExpr(std::string varName, bool isConst, Expr *iterable,
          CompoundStmt *body, std::string label, SourceLocation loc)
      : Expr(ExprKind::For, loc), varName(std::move(varName)),
        isConst(isConst), iterable(iterable), body(body),
        label(std::move(label)) {}

  llvm::StringRef getVarName() const { return varName; }
  bool getIsConst() const { return isConst; }
  Expr *getIterable() const { return iterable; }
  CompoundStmt *getBody() const { return body; }
  llvm::StringRef getLabel() const { return label; }

  static bool classof(const Expr *e) { return e->getKind() == ExprKind::For; }

private:
  std::string varName;
  bool isConst;
  Expr *iterable;
  CompoundStmt *body;
  std::string label;
};

// --- Closure ---

struct ClosureParam {
  std::string name;
  Type *type; // nullable
  SourceLocation loc;
};

class ClosureExpr : public Expr {
public:
  ClosureExpr(std::vector<ClosureParam> params, Type *returnType,
              Expr *body, SourceLocation loc)
      : Expr(ExprKind::Closure, loc), params(std::move(params)),
        returnType(returnType), body(body) {}

  const std::vector<ClosureParam> &getParams() const { return params; }
  Type *getReturnType() const { return returnType; }
  Expr *getBody() const { return body; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Closure;
  }

private:
  std::vector<ClosureParam> params;
  Type *returnType;
  Expr *body;
};

// --- Assignment ---

enum class AssignOp {
  Assign,  // =
  AddAssign, SubAssign, MulAssign, DivAssign, ModAssign,
  BitAndAssign, BitOrAssign, BitXorAssign, ShlAssign, ShrAssign,
};

class AssignExpr : public Expr {
public:
  AssignExpr(AssignOp op, Expr *target, Expr *value, SourceLocation loc)
      : Expr(ExprKind::Assign, loc), op(op), target(target), value(value) {}

  AssignOp getOp() const { return op; }
  Expr *getTarget() const { return target; }
  Expr *getValue() const { return value; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Assign;
  }

private:
  AssignOp op;
  Expr *target;
  Expr *value;
};

class BlockExpr : public Expr {
public:
  BlockExpr(CompoundStmt *block, SourceLocation loc)
      : Expr(ExprKind::Block, loc), block(block) {}

  CompoundStmt *getBlock() const { return block; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Block;
  }

private:
  CompoundStmt *block;
};

class MacroCallExpr : public Expr {
public:
  MacroCallExpr(std::string macroName, std::vector<Expr *> args,
                SourceLocation loc)
      : Expr(ExprKind::MacroCall, loc), macroName(std::move(macroName)),
        args(std::move(args)) {}

  llvm::StringRef getMacroName() const { return macroName; }
  const std::vector<Expr *> &getArgs() const { return args; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::MacroCall;
  }

private:
  std::string macroName;
  std::vector<Expr *> args;
};

class UnsafeBlockExpr : public Expr {
public:
  UnsafeBlockExpr(CompoundStmt *body, SourceLocation loc)
      : Expr(ExprKind::UnsafeBlock, loc), body(body) {}

  CompoundStmt *getBody() const { return body; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::UnsafeBlock;
  }

private:
  CompoundStmt *body;
};

/// Template literal parts.
struct TemplatePart {
  std::string text;
  Expr *expr; // nullptr for the final text part
};

class TemplateLiteralExpr : public Expr {
public:
  TemplateLiteralExpr(std::vector<TemplatePart> parts, SourceLocation loc)
      : Expr(ExprKind::TemplateLiteral, loc), parts(std::move(parts)) {}

  const std::vector<TemplatePart> &getParts() const { return parts; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::TemplateLiteral;
  }

private:
  std::vector<TemplatePart> parts;
};

/// `expr?` — error propagation
class TryExpr : public Expr {
public:
  TryExpr(Expr *operand, SourceLocation loc)
      : Expr(ExprKind::Try, loc), operand(operand) {}

  Expr *getOperand() const { return operand; }

  static bool classof(const Expr *e) { return e->getKind() == ExprKind::Try; }

private:
  Expr *operand;
};

/// `Foo::Bar::baz` — path expression
class PathExpr : public Expr {
public:
  PathExpr(std::vector<std::string> segments,
           std::vector<Type *> genericArgs, SourceLocation loc)
      : Expr(ExprKind::Path, loc), segments(std::move(segments)),
        genericArgs(std::move(genericArgs)) {}

  const std::vector<std::string> &getSegments() const { return segments; }
  const std::vector<Type *> &getGenericArgs() const { return genericArgs; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Path;
  }

private:
  std::vector<std::string> segments;
  std::vector<Type *> genericArgs;
};

/// Parenthesized expression `(expr)`
class ParenExpr : public Expr {
public:
  ParenExpr(Expr *inner, SourceLocation loc)
      : Expr(ExprKind::Paren, loc), inner(inner) {}

  Expr *getInner() const { return inner; }

  static bool classof(const Expr *e) {
    return e->getKind() == ExprKind::Paren;
  }

private:
  Expr *inner;
};

// --- Patterns ---

enum class PatternKind {
  Literal,
  Ident,
  Tuple,
  Struct,
  Enum,
  Slice,
  Range,
  Wildcard,
  Or,
  Guard,
};

class Pattern {
public:
  virtual ~Pattern() = default;
  PatternKind getKind() const { return kind; }
  SourceLocation getLocation() const { return loc; }

protected:
  Pattern(PatternKind kind, SourceLocation loc) : kind(kind), loc(loc) {}

private:
  PatternKind kind;
  SourceLocation loc;
};

class LiteralPattern : public Pattern {
public:
  LiteralPattern(Expr *literal, SourceLocation loc)
      : Pattern(PatternKind::Literal, loc), literal(literal) {}

  Expr *getLiteral() const { return literal; }

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Literal;
  }

private:
  Expr *literal;
};

class IdentPattern : public Pattern {
public:
  IdentPattern(std::string name, bool isMut, SourceLocation loc)
      : Pattern(PatternKind::Ident, loc), name(std::move(name)),
        isMut(isMut) {}

  llvm::StringRef getName() const { return name; }
  bool getIsMut() const { return isMut; }

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Ident;
  }

private:
  std::string name;
  bool isMut;
};

class TuplePattern : public Pattern {
public:
  TuplePattern(std::vector<Pattern *> elements, SourceLocation loc)
      : Pattern(PatternKind::Tuple, loc), elements(std::move(elements)) {}

  const std::vector<Pattern *> &getElements() const { return elements; }

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Tuple;
  }

private:
  std::vector<Pattern *> elements;
};

struct FieldPattern {
  std::string name;
  Pattern *pattern; // nullable for shorthand
  SourceLocation loc;
};

class StructPattern : public Pattern {
public:
  StructPattern(std::string typeName, std::vector<FieldPattern> fields,
                SourceLocation loc)
      : Pattern(PatternKind::Struct, loc), typeName(std::move(typeName)),
        fields(std::move(fields)) {}

  llvm::StringRef getTypeName() const { return typeName; }
  const std::vector<FieldPattern> &getFields() const { return fields; }

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Struct;
  }

private:
  std::string typeName;
  std::vector<FieldPattern> fields;
};

class EnumPattern : public Pattern {
public:
  EnumPattern(std::vector<std::string> path, std::vector<Pattern *> args,
              SourceLocation loc)
      : Pattern(PatternKind::Enum, loc), path(std::move(path)),
        args(std::move(args)) {}

  const std::vector<std::string> &getPath() const { return path; }
  const std::vector<Pattern *> &getArgs() const { return args; }

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Enum;
  }

private:
  std::vector<std::string> path;
  std::vector<Pattern *> args;
};

class SlicePattern : public Pattern {
public:
  SlicePattern(std::vector<Pattern *> elements, std::string rest,
               SourceLocation loc)
      : Pattern(PatternKind::Slice, loc), elements(std::move(elements)),
        rest(std::move(rest)) {}

  const std::vector<Pattern *> &getElements() const { return elements; }
  llvm::StringRef getRest() const { return rest; }

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Slice;
  }

private:
  std::vector<Pattern *> elements;
  std::string rest; // empty if no rest pattern
};

class RangePattern : public Pattern {
public:
  RangePattern(Expr *start, Expr *end, bool inclusive, SourceLocation loc)
      : Pattern(PatternKind::Range, loc), start(start), end(end),
        inclusive(inclusive) {}

  Expr *getStart() const { return start; }
  Expr *getEnd() const { return end; }
  bool isInclusive() const { return inclusive; }

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Range;
  }

private:
  Expr *start;
  Expr *end;
  bool inclusive;
};

class WildcardPattern : public Pattern {
public:
  explicit WildcardPattern(SourceLocation loc)
      : Pattern(PatternKind::Wildcard, loc) {}

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Wildcard;
  }
};

class OrPattern : public Pattern {
public:
  OrPattern(std::vector<Pattern *> alternatives, SourceLocation loc)
      : Pattern(PatternKind::Or, loc), alternatives(std::move(alternatives)) {}

  const std::vector<Pattern *> &getAlternatives() const {
    return alternatives;
  }

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Or;
  }

private:
  std::vector<Pattern *> alternatives;
};

class GuardPattern : public Pattern {
public:
  GuardPattern(Pattern *inner, Expr *guard, SourceLocation loc)
      : Pattern(PatternKind::Guard, loc), inner(inner), guard(guard) {}

  Pattern *getInner() const { return inner; }
  Expr *getGuard() const { return guard; }

  static bool classof(const Pattern *p) {
    return p->getKind() == PatternKind::Guard;
  }

private:
  Pattern *inner;
  Expr *guard;
};

} // namespace asc

#endif // ASC_AST_EXPR_H
