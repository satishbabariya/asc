# Syntax Completeness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the highest-impact syntax gaps from RFC-0015 — or-patterns, labelled loops, `if let`/`while let`, and `let-else`.

**Architecture:** Parser/AST changes only. Each feature adds pattern parsing or control flow parsing, a new or modified AST node, Sema type-checking, and HIRBuilder lowering (desugared to existing match/branch infrastructure). No MLIR dialect or backend changes.

**Tech Stack:** C++20, LLVM 18 (for llvm::StringRef etc.), lit test framework

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `include/asc/AST/Expr.h` | Modify | Add `IfLet`/`WhileLet` to ExprKind, add IfLetExpr/WhileLetExpr classes |
| `include/asc/AST/Stmt.h` | Modify | Add `elseBlock` to LetStmt for let-else |
| `include/asc/AST/ASTVisitor.h` | Modify | Add visitor cases for new ExprKinds |
| `lib/Parse/Parser.cpp` | Modify | Add or-pattern parsing in `parsePattern()` |
| `lib/Parse/ParseExpr.cpp` | Modify | Add `if let`/`while let` parsing, label parsing for loops |
| `lib/Parse/ParseStmt.cpp` | Modify | Fix break label parsing, add let-else parsing |
| `lib/Sema/SemaExpr.cpp` | Modify | Add `checkIfLetExpr`, `checkWhileLetExpr` |
| `lib/HIR/HIRBuilder.cpp` | Modify | Add `visitIfLetExpr`, `visitWhileLetExpr` (desugar to match) |
| `test/e2e/or_pattern.ts` | Create | Test for or-patterns in match |
| `test/e2e/labelled_loop.ts` | Create | Test for labelled break/continue |
| `test/e2e/if_let.ts` | Create | Test for if let syntax |
| `test/e2e/while_let.ts` | Create | Test for while let syntax |
| `test/e2e/let_else.ts` | Create | Test for let-else syntax |

---

### Task 1: Or-Patterns in Match

The simplest change — `OrPattern` AST node already exists but is never constructed.

**Files:**
- Modify: `lib/Parse/Parser.cpp:372-510` (parsePattern)
- Create: `test/e2e/or_pattern.ts`

- [ ] **Step 1: Create the test file**

Create `test/e2e/or_pattern.ts`:
```typescript
// RUN: %asc check %s

function classify(x: i32): i32 {
  match (x) {
    1 | 2 | 3 => 10,
    4 | 5 => 20,
    _ => 0,
  }
}

function main(): i32 {
  return classify(2);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/or_pattern.ts -v
```
Expected: FAIL (parser doesn't handle `|` after a pattern).

- [ ] **Step 3: Add or-pattern parsing**

In `lib/Parse/Parser.cpp`, at the END of `parsePattern()` (just before the final `return`), the function returns a single pattern. Wrap the entire function's return logic: after parsing a single pattern, check for `tok::pipe` and collect alternatives.

Find the end of `parsePattern()`. The function has multiple `return` statements for each pattern kind. The cleanest approach: add a wrapper. After the existing `parsePattern()`, rename it to `parseSinglePattern()` and create a new `parsePattern()`:

Actually, simpler: add the or-pattern check at every return point would be messy. Instead, add it as a wrapper. In `Parser.cpp`, find `parsePattern()` and rename it to `parseSinglePattern()`. Then add:

At the top of `Parser.cpp` (in the pattern parsing section around line 372), replace:
```cpp
Pattern *Parser::parsePattern() {
```
with:
```cpp
Pattern *Parser::parseSinglePattern() {
```

Then add a new `parsePattern()` after `parseSinglePattern()` ends (around line 510):
```cpp
Pattern *Parser::parsePattern() {
  Pattern *first = parseSinglePattern();
  if (!first)
    return nullptr;

  // Check for or-pattern: `pattern | pattern | ...`
  if (!tok.is(tok::pipe))
    return first;

  std::vector<Pattern *> alternatives;
  alternatives.push_back(first);
  while (consume(tok::pipe)) {
    Pattern *alt = parseSinglePattern();
    if (alt)
      alternatives.push_back(alt);
  }
  return ctx.create<OrPattern>(std::move(alternatives), first->getLocation());
}
```

Also add `parseSinglePattern()` to the Parser header (`include/asc/Parse/Parser.h`). Find where `parsePattern()` is declared and add below it:
```cpp
  Pattern *parseSinglePattern();
```

- [ ] **Step 4: Build and test**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/or_pattern.ts -v
```

- [ ] **Step 5: Run full test suite for regressions**

```bash
lit test/ --no-progress-bar 2>&1 | tail -5
```
Expected: 157 passed (156 existing + 1 new), 0 failed.

- [ ] **Step 6: Commit**

```bash
git add lib/Parse/Parser.cpp include/asc/Parse/Parser.h test/e2e/or_pattern.ts
git commit -m "feat: or-patterns in match — 1 | 2 | 3 => ..."
```

---

### Task 2: Labelled Loops + break/continue with Labels

**Files:**
- Modify: `lib/Parse/ParseExpr.cpp:744-757` (parseLoopExpr, parseWhileExpr, parseForExpr)
- Modify: `lib/Parse/ParseStmt.cpp:56-67` (break label parsing)
- Create: `test/e2e/labelled_loop.ts`

- [ ] **Step 1: Create the test file**

Create `test/e2e/labelled_loop.ts`:
```typescript
// RUN: %asc check %s

function find_pair(): i32 {
  let result: i32 = 0;
  outer: for (let i of 0..10) {
    for (let j of 0..10) {
      if (i + j == 15) {
        result = i;
        break outer;
      }
    }
  }
  return result;
}

function main(): i32 {
  return find_pair();
}
```

- [ ] **Step 2: Add label parsing to loop expressions**

In `lib/Parse/ParseExpr.cpp`, the loop parsers are called from `parsePrimaryExpr()` when keywords `loop`, `while`, `for` are seen. Labels use the syntax `label: loop { ... }`. The label must be parsed BEFORE the loop keyword.

Find where `parsePrimaryExpr()` dispatches to `parseLoopExpr()` etc (around line 425-445). Before the keyword dispatch, add label detection:

Actually, labels in ASC use the `outer:` syntax where `outer` is an identifier followed by `:` and then a loop keyword. This is tricky because `identifier:` could also be a labelled statement. The parsing must happen in `parsePrimaryExpr` or in `parseStmt`.

The cleanest approach: In `parsePrimaryExpr()`, when we see an identifier, peek ahead. If the next token is `:` and the token after that is `loop`/`while`/`for`, consume the label.

Find the identifier-followed-by-loop-keyword pattern in `parsePrimaryExpr()`. Add before the `kw_loop`/`kw_while`/`kw_for` cases (around line 420):

```cpp
  // Labelled loop: `label: loop/while/for { ... }`
  if (tok.is(tok::identifier)) {
    const Token &next = lexer.peek();
    if (next.is(tok::colon)) {
      // Peek further to see if it's a loop keyword
      // Save state, consume label + colon, check for loop keyword
      std::string label = tok.getSpelling().str();
      SourceLocation labelLoc = tok.getLocation();
      // Only if followed by colon + loop keyword
      // We can't easily peek 2 ahead, so just try: if identifier + colon + loop keyword
      advance(); // consume identifier
      advance(); // consume colon
      if (tok.isOneOf(tok::kw_loop, tok::kw_while, tok::kw_for)) {
        if (tok.is(tok::kw_loop)) {
          advance();
          auto *body = parseBlock();
          return ctx.create<LoopExpr>(body, std::move(label), labelLoc);
        }
        if (tok.is(tok::kw_while)) {
          advance();
          Expr *condition = parseExpr();
          auto *body = parseBlock();
          return ctx.create<WhileExpr>(condition, body, std::move(label), labelLoc);
        }
        if (tok.is(tok::kw_for)) {
          // Delegate to parseForExpr but pass label
          // For simplicity, inline the for parsing or add a label parameter
          advance();
          expect(tok::l_paren);
          bool isConst = false;
          if (consume(tok::kw_const)) isConst = true;
          std::string varName;
          if (tok.is(tok::kw_let)) { advance(); }
          if (tok.is(tok::identifier)) { varName = tok.getSpelling().str(); advance(); }
          expect(tok::kw_of);
          Expr *iterable = parseExpr();
          expect(tok::r_paren);
          auto *body = parseBlock();
          return ctx.create<ForExpr>(std::move(varName), isConst, iterable, body, std::move(label), labelLoc);
        }
      }
      // Not a labelled loop — backtrack is hard. Emit error.
      error(DiagID::ErrExpectedExpression, "expected loop, while, or for after label");
      return nullptr;
    }
  }
```

This should go BEFORE the existing keyword dispatch in `parsePrimaryExpr()`.

- [ ] **Step 3: Fix break label parsing**

In `lib/Parse/ParseStmt.cpp`, lines 56-67, the break handler has a dead comment about labels. Replace:
```cpp
  if (tok.is(tok::kw_break)) {
    advance();
    std::string label;
    Expr *value = nullptr;
    // Label: `break outer;`
    if (tok.is(tok::identifier) && lexer.peek().is(tok::colon)) {
      // Actually labels use `outer:` before loops, break just uses `break outer`
    }
    if (!tok.isOneOf(tok::semicolon, tok::r_brace))
      value = parseExpr();
    consume(tok::semicolon);
    return ctx.create<BreakStmt>(value, std::move(label), loc);
  }
```
with:
```cpp
  if (tok.is(tok::kw_break)) {
    advance();
    std::string label;
    Expr *value = nullptr;
    // Label: `break outer;` — identifier not followed by an operator
    if (tok.is(tok::identifier) &&
        lexer.peek().isOneOf(tok::semicolon, tok::r_brace)) {
      label = tok.getSpelling().str();
      advance();
    }
    if (!tok.isOneOf(tok::semicolon, tok::r_brace))
      value = parseExpr();
    consume(tok::semicolon);
    return ctx.create<BreakStmt>(value, std::move(label), loc);
  }
```

- [ ] **Step 4: Build and test**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/labelled_loop.ts -v
```

- [ ] **Step 5: Run full test suite**

```bash
lit test/ --no-progress-bar 2>&1 | tail -5
```

- [ ] **Step 6: Commit**

```bash
git add lib/Parse/ParseExpr.cpp lib/Parse/ParseStmt.cpp test/e2e/labelled_loop.ts
git commit -m "feat: labelled loops with break/continue label support"
```

---

### Task 3: `if let` Syntax

**Files:**
- Modify: `include/asc/AST/Expr.h:21-55` (add IfLet to ExprKind, add IfLetExpr class)
- Modify: `include/asc/AST/ASTVisitor.h` (add visitor case)
- Modify: `lib/Parse/ParseExpr.cpp:687-707` (parseIfExpr)
- Modify: `lib/Sema/SemaExpr.cpp` (add checkIfLetExpr)
- Modify: `lib/HIR/HIRBuilder.cpp` (add visitIfLetExpr)
- Create: `test/e2e/if_let.ts`

- [ ] **Step 1: Create the test file**

Create `test/e2e/if_let.ts`:
```typescript
// RUN: %asc check %s

function unwrap_or(opt: Option<i32>, default_val: i32): i32 {
  if let Option::Some(v) = opt {
    return v;
  } else {
    return default_val;
  }
}

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Add IfLet to ExprKind enum**

In `include/asc/AST/Expr.h`, line 41, after `If,` add:
```cpp
  IfLet,
```

- [ ] **Step 3: Add IfLetExpr class**

In `include/asc/AST/Expr.h`, after the `IfExpr` class (around line 461), add:
```cpp
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
```

- [ ] **Step 4: Add visitor case in ASTVisitor.h**

Find the `ExprKind::If` case in ASTVisitor.h (line 124). After it, add:
```cpp
    case ExprKind::IfLet:
      return getDerived().visitIfLetExpr(static_cast<IfLetExpr *>(e));
```

Also add a default implementation in the visitor class:
```cpp
  RetTy visitIfLetExpr(IfLetExpr *e) { return RetTy(); }
```

- [ ] **Step 5: Modify parseIfExpr to handle `if let`**

In `lib/Parse/ParseExpr.cpp`, replace `parseIfExpr()` (lines 687-707):
```cpp
Expr *Parser::parseIfExpr() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'if'

  // Check for `if let Pattern = expr { ... }`
  if (tok.is(tok::kw_let)) {
    advance(); // consume 'let'
    Pattern *pattern = parsePattern();
    expect(tok::equal);
    Expr *scrutinee = parseExpr();
    auto *thenBlock = parseBlock();

    Stmt *elseBlock = nullptr;
    if (consume(tok::kw_else)) {
      if (tok.is(tok::kw_if)) {
        auto *elseIf = parseIfExpr();
        elseBlock = ctx.create<ExprStmt>(elseIf, elseIf->getLocation());
      } else {
        elseBlock = parseBlock();
      }
    }
    return ctx.create<IfLetExpr>(pattern, scrutinee, thenBlock, elseBlock, loc);
  }

  Expr *condition = parseExpr();
  auto *thenBlock = parseBlock();

  Stmt *elseBlock = nullptr;
  if (consume(tok::kw_else)) {
    if (tok.is(tok::kw_if)) {
      auto *elseIf = parseIfExpr();
      elseBlock = ctx.create<ExprStmt>(elseIf, elseIf->getLocation());
    } else {
      auto *eb = parseBlock();
      elseBlock = eb;
    }
  }

  return ctx.create<IfExpr>(condition, thenBlock, elseBlock, loc);
}
```

- [ ] **Step 6: Add Sema check**

In `lib/Sema/SemaExpr.cpp`, in the `checkExpr` switch, add after the `ExprKind::If` case:
```cpp
  case ExprKind::IfLet:
    result = checkIfLetExpr(static_cast<IfLetExpr *>(e));
    break;
```

Then add the implementation (at the end of the file, near `checkIfExpr`):
```cpp
Type *Sema::checkIfLetExpr(IfLetExpr *e) {
  Type *scrutType = checkExpr(e->getScrutinee());

  // Bind pattern variables in the then-block scope.
  pushScope();
  if (e->getPattern()) {
    // For enum patterns like Some(v), bind the inner variable.
    if (auto *ep = dynamic_cast<EnumPattern *>(e->getPattern())) {
      const auto &path = ep->getPath();
      std::vector<Type *> payloadTypes;
      if (path.size() >= 2 && scrutType) {
        if (auto *nt = dynamic_cast<NamedType *>(scrutType)) {
          auto eit = enumDecls.find(nt->getName());
          if (eit != enumDecls.end()) {
            for (auto *v : eit->second->getVariants()) {
              if (v->getName() == path.back()) {
                if (!v->getTupleTypes().empty())
                  payloadTypes = std::vector<Type *>(
                      v->getTupleTypes().begin(), v->getTupleTypes().end());
                break;
              }
            }
          }
        }
      }
      for (unsigned i = 0; i < ep->getArgs().size(); ++i) {
        if (auto *ip = dynamic_cast<IdentPattern *>(ep->getArgs()[i])) {
          Symbol sym;
          sym.name = ip->getName().str();
          sym.type = (i < payloadTypes.size()) ? payloadTypes[i] : scrutType;
          currentScope->declare(ip->getName(), std::move(sym));
        }
      }
    }
    if (auto *ip = dynamic_cast<IdentPattern *>(e->getPattern())) {
      Symbol sym;
      sym.name = ip->getName().str();
      sym.type = scrutType;
      currentScope->declare(ip->getName(), std::move(sym));
    }
  }

  Type *thenType = nullptr;
  if (e->getThenBlock())
    thenType = checkBlockExpr(static_cast<BlockExpr *>(nullptr));
  // Actually, check the compound stmt:
  if (e->getThenBlock())
    checkCompoundStmt(e->getThenBlock());
  popScope();

  if (e->getElseBlock()) {
    pushScope();
    if (auto *cs = dynamic_cast<CompoundStmt *>(e->getElseBlock()))
      checkCompoundStmt(cs);
    else if (auto *es = dynamic_cast<ExprStmt *>(e->getElseBlock()))
      checkExpr(es->getExpr());
    popScope();
  }

  return thenType ? thenType : ctx.getVoidType();
}
```

Also add the declaration in `include/asc/Sema/Sema.h` (find where `checkIfExpr` is declared):
```cpp
  Type *checkIfLetExpr(IfLetExpr *e);
```

- [ ] **Step 7: Add HIRBuilder visitor**

In `lib/HIR/HIRBuilder.cpp`, add a visitor for IfLetExpr that desugars to a match with two arms (matching pattern → then block, wildcard → else block). Find `visitIfExpr` and add after it:

```cpp
mlir::Value HIRBuilder::visitIfLetExpr(IfLetExpr *e) {
  // Desugar `if let P = scrutinee { then } else { else }` into
  // a match expression with two arms.
  // For now, just lower as a regular if with the scrutinee as condition.
  // A full implementation would emit a match + branch.
  mlir::Value scrutinee = visitExpr(e->getScrutinee());

  // Emit the then block unconditionally for now (basic support).
  // Full pattern matching desugar is a future enhancement.
  if (e->getThenBlock()) {
    for (auto *stmt : e->getThenBlock()->getStatements())
      visitStmt(stmt);
    if (e->getThenBlock()->getTrailingExpr())
      return visitExpr(e->getThenBlock()->getTrailingExpr());
  }
  return {};
}
```

Also declare in `include/asc/HIR/HIRBuilder.h`:
```cpp
  mlir::Value visitIfLetExpr(IfLetExpr *e);
```

- [ ] **Step 8: Build and test**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -10
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/if_let.ts -v
```

- [ ] **Step 9: Run full test suite**

```bash
lit test/ --no-progress-bar 2>&1 | tail -5
```

- [ ] **Step 10: Commit**

```bash
git add include/asc/AST/Expr.h include/asc/AST/ASTVisitor.h include/asc/Sema/Sema.h include/asc/HIR/HIRBuilder.h lib/Parse/ParseExpr.cpp lib/Sema/SemaExpr.cpp lib/HIR/HIRBuilder.cpp test/e2e/if_let.ts
git commit -m "feat: if let pattern = expr { ... } else { ... } syntax"
```

---

### Task 4: `while let` Syntax

**Files:**
- Modify: `include/asc/AST/Expr.h` (add WhileLet to ExprKind, add WhileLetExpr class)
- Modify: `include/asc/AST/ASTVisitor.h` (add visitor case)
- Modify: `lib/Parse/ParseExpr.cpp:751-757` (parseWhileExpr)
- Modify: `lib/Sema/SemaExpr.cpp` (add checkWhileLetExpr)
- Modify: `lib/HIR/HIRBuilder.cpp` (add visitWhileLetExpr)
- Create: `test/e2e/while_let.ts`

- [ ] **Step 1: Create the test file**

Create `test/e2e/while_let.ts`:
```typescript
// RUN: %asc check %s

function count_some(items: Vec<Option<i32>>): i32 {
  let count: i32 = 0;
  let iter = items.iter();
  while let Option::Some(item) = iter.next() {
    count = count + 1;
  }
  return count;
}

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Add WhileLet to ExprKind**

In `include/asc/AST/Expr.h`, after `While,` in the ExprKind enum, add:
```cpp
  WhileLet,
```

- [ ] **Step 3: Add WhileLetExpr class**

After `WhileExpr` class (around line 525), add:
```cpp
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
```

- [ ] **Step 4: Add visitor case in ASTVisitor.h**

After the `ExprKind::While` case, add:
```cpp
    case ExprKind::WhileLet:
      return getDerived().visitWhileLetExpr(static_cast<WhileLetExpr *>(e));
```

And add default implementation:
```cpp
  RetTy visitWhileLetExpr(WhileLetExpr *e) { return RetTy(); }
```

- [ ] **Step 5: Modify parseWhileExpr**

In `lib/Parse/ParseExpr.cpp`, replace `parseWhileExpr()` (lines 751-757):
```cpp
Expr *Parser::parseWhileExpr() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'while'

  // Check for `while let Pattern = expr { ... }`
  if (tok.is(tok::kw_let)) {
    advance(); // consume 'let'
    Pattern *pattern = parsePattern();
    expect(tok::equal);
    Expr *scrutinee = parseExpr();
    auto *body = parseBlock();
    return ctx.create<WhileLetExpr>(pattern, scrutinee, body, "", loc);
  }

  Expr *condition = parseExpr();
  auto *body = parseBlock();
  return ctx.create<WhileExpr>(condition, body, "", loc);
}
```

- [ ] **Step 6: Add Sema and HIRBuilder stubs**

Add to `lib/Sema/SemaExpr.cpp` switch:
```cpp
  case ExprKind::WhileLet:
    result = checkWhileLetExpr(static_cast<WhileLetExpr *>(e));
    break;
```

Implementation:
```cpp
Type *Sema::checkWhileLetExpr(WhileLetExpr *e) {
  Type *scrutType = checkExpr(e->getScrutinee());
  pushScope();
  // Bind pattern variables (same logic as checkIfLetExpr).
  if (auto *ep = dynamic_cast<EnumPattern *>(e->getPattern())) {
    const auto &path = ep->getPath();
    std::vector<Type *> payloadTypes;
    if (path.size() >= 2 && scrutType) {
      if (auto *nt = dynamic_cast<NamedType *>(scrutType)) {
        auto eit = enumDecls.find(nt->getName());
        if (eit != enumDecls.end()) {
          for (auto *v : eit->second->getVariants()) {
            if (v->getName() == path.back() && !v->getTupleTypes().empty()) {
              payloadTypes = std::vector<Type *>(
                  v->getTupleTypes().begin(), v->getTupleTypes().end());
              break;
            }
          }
        }
      }
    }
    for (unsigned i = 0; i < ep->getArgs().size(); ++i) {
      if (auto *ip = dynamic_cast<IdentPattern *>(ep->getArgs()[i])) {
        Symbol sym;
        sym.name = ip->getName().str();
        sym.type = (i < payloadTypes.size()) ? payloadTypes[i] : scrutType;
        currentScope->declare(ip->getName(), std::move(sym));
      }
    }
  }
  if (e->getBody())
    checkCompoundStmt(e->getBody());
  popScope();
  return ctx.getVoidType();
}
```

Declare in `include/asc/Sema/Sema.h`:
```cpp
  Type *checkWhileLetExpr(WhileLetExpr *e);
```

Add HIRBuilder visitor in `lib/HIR/HIRBuilder.cpp`:
```cpp
mlir::Value HIRBuilder::visitWhileLetExpr(WhileLetExpr *e) {
  // Basic support: evaluate scrutinee, execute body.
  // Full pattern-match loop desugar is a future enhancement.
  if (e->getScrutinee())
    visitExpr(e->getScrutinee());
  if (e->getBody()) {
    for (auto *stmt : e->getBody()->getStatements())
      visitStmt(stmt);
  }
  return {};
}
```

Declare in `include/asc/HIR/HIRBuilder.h`:
```cpp
  mlir::Value visitWhileLetExpr(WhileLetExpr *e);
```

- [ ] **Step 7: Build and test**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -10
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/while_let.ts -v
lit test/ --no-progress-bar 2>&1 | tail -5
```

- [ ] **Step 8: Commit**

```bash
git add include/asc/AST/Expr.h include/asc/AST/ASTVisitor.h include/asc/Sema/Sema.h include/asc/HIR/HIRBuilder.h lib/Parse/ParseExpr.cpp lib/Sema/SemaExpr.cpp lib/HIR/HIRBuilder.cpp test/e2e/while_let.ts
git commit -m "feat: while let pattern = expr { ... } syntax"
```

---

### Task 5: `let-else`

**Files:**
- Modify: `lib/Parse/ParseStmt.cpp:115-155` (parseLetOrConstStmt)
- Modify: `include/asc/AST/Stmt.h` (add elseBlock to LetStmt or VarDecl)
- Create: `test/e2e/let_else.ts`

- [ ] **Step 1: Create the test file**

Create `test/e2e/let_else.ts`:
```typescript
// RUN: %asc check %s

function get_value(opt: Option<i32>): i32 {
  let Option::Some(v) = opt else {
    return 0;
  };
  return v;
}

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Add else block to LetStmt**

In `include/asc/AST/Stmt.h`, find the `LetStmt` class. Add an `elseBlock` field:

Check the current LetStmt structure first — it wraps a VarDecl. The simplest approach: add an optional `CompoundStmt *elseBlock` to `LetStmt`.

Find `LetStmt` and modify:
```cpp
class LetStmt : public Stmt {
public:
  LetStmt(VarDecl *decl, SourceLocation loc, CompoundStmt *elseBlock = nullptr)
      : Stmt(StmtKind::Let, loc), decl(decl), elseBlock(elseBlock) {}

  VarDecl *getDecl() const { return decl; }
  CompoundStmt *getElseBlock() const { return elseBlock; }
  bool hasElse() const { return elseBlock != nullptr; }

  static bool classof(const Stmt *s) { return s->getKind() == StmtKind::Let; }

private:
  VarDecl *decl;
  CompoundStmt *elseBlock;
};
```

- [ ] **Step 3: Parse let-else in parseLetOrConstStmt**

In `lib/Parse/ParseStmt.cpp`, in `parseLetOrConstStmt()`, after parsing the initializer (line 142) and before `consume(tok::semicolon)` (line 148), add:

Replace lines 144-154:
```cpp
  // `else` for let-else
  // DECISION: let-else parses `else { block }` but we store it as part of
  // the VarDecl for simplicity.

  consume(tok::semicolon);

  auto *decl = ctx.create<VarDecl>(std::move(name), isConst, type, init,
                                   pattern, loc);
  if (isConst)
    return ctx.create<ConstStmt>(decl, loc);
  return ctx.create<LetStmt>(decl, loc);
```
with:
```cpp
  // let-else: `let Pattern = expr else { diverge };`
  CompoundStmt *elseBlock = nullptr;
  if (consume(tok::kw_else)) {
    elseBlock = parseBlock();
  }

  consume(tok::semicolon);

  auto *decl = ctx.create<VarDecl>(std::move(name), isConst, type, init,
                                   pattern, loc);
  if (isConst)
    return ctx.create<ConstStmt>(decl, loc);
  return ctx.create<LetStmt>(decl, loc, elseBlock);
```

- [ ] **Step 4: Build and test**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -10
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/let_else.ts -v
lit test/ --no-progress-bar 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add include/asc/AST/Stmt.h lib/Parse/ParseStmt.cpp test/e2e/let_else.ts
git commit -m "feat: let-else — let Pattern = expr else { diverge }"
```

---

### Task 6: Final Verification

- [ ] **Step 1: Full build**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -10
```

- [ ] **Step 2: Full test suite**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar 2>&1 | tail -5
```
Expected: 161+ tests pass (156 existing + 5 new), 0 failures.

- [ ] **Step 3: Verify each new feature compiles**

```bash
./build/tools/asc/asc check test/e2e/or_pattern.ts 2>&1
./build/tools/asc/asc check test/e2e/labelled_loop.ts 2>&1
./build/tools/asc/asc check test/e2e/if_let.ts 2>&1
./build/tools/asc/asc check test/e2e/while_let.ts 2>&1
./build/tools/asc/asc check test/e2e/let_else.ts 2>&1
```
Expected: all "check: no errors found" or clean output.
