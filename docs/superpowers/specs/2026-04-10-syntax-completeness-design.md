# Syntax Completeness Push — Design Spec

**Date:** 2026-04-10
**Goal:** Close the highest-impact syntax gaps from RFC-0015 — parser/AST only, no backend changes.
**Risk:** Low — all changes are in the parser and AST. 156 regression tests as safety net.

---

## Feature 1: `if let` / `while let`

**Problem:** `if let Some(v) = expr { ... }` and `while let Some(v) = iter.next() { ... }` are not parsed.

**Fix:**
- In `parseIfExpr()`: after consuming `if`, check for `kw_let`. If present, parse pattern + `=` + expr, creating an `IfLetExpr` (new AST node) instead of `IfExpr`.
- In `parseWhileExpr()`: same pattern — check for `kw_let` after `while`.
- New AST nodes: `IfLetExpr(pattern, scrutinee, thenBlock, elseBlock)` and `WhileLetExpr(pattern, scrutinee, body, label)`.
- Sema: `checkIfLetExpr` binds pattern variables in the then-block scope, type-checks against scrutinee.
- HIRBuilder: desugar `if let` to match + branch (already has match infrastructure).

## Feature 2: Or-Patterns in Match

**Problem:** `match x { 1 | 2 | 3 => ... }` doesn't parse — `parsePattern()` has no `|` handling.

**Fix:**
- After parsing a single pattern in `parsePattern()`, check for `tok::pipe`. If present, parse additional patterns and wrap in `OrPattern(patterns)`.
- New AST node: `OrPattern` (already declared in the PatternKind enum as `Or` but never constructed).
- Sema: all sub-patterns must bind the same variables with compatible types.

## Feature 3: Labelled Loops + break/continue with Labels

**Problem:** `LoopExpr`, `WhileExpr`, `ForExpr` have `label` fields but the parser always passes `""`. `break` and `continue` never consume the label token.

**Fix:**
- In `parseLoopExpr`, `parseWhileExpr`, `parseForExpr`: before parsing the keyword, check if the current token is an identifier followed by `:` (label syntax). If so, consume both and pass the label to the constructor.
- In `parseBreakStmt`: after `break`, check for identifier (label). If present, consume it.
- In `parseContinueStmt`: already handles labels — verify it works.

## Feature 4: `let-else`

**Problem:** `let Some(v) = expr else { return; }` — the parser has a comment about let-else but never consumes the `else` branch.

**Fix:**
- In `parseLetOrConstStmt`: after parsing the initializer, check for `kw_else`. If present, parse a block as the divergent path.
- Add `elseBlock` field to `LetStmt` (or create `LetElseStmt`).
- Sema: verify the else block diverges (returns/breaks/continues/panics).

## Out of Scope

- Associated constants in traits/impl (requires Sema + HIR changes)
- Single-param untyped closures (parser ambiguity with parenthesized exprs)
- `>>` nested generics fix (fragile but working)
- Raw pointer types (unsafe FFI — separate concern)

## Success Criteria

1. All 156 existing tests still pass.
2. `if let Some(v) = opt { v }` parses and type-checks.
3. `while let Some(v) = iter.next() { ... }` parses.
4. `match x { 1 | 2 | 3 => ... }` parses.
5. `'outer: for (...) { break 'outer; }` parses.
6. `let Some(v) = expr else { return 0; }` parses.
7. New e2e tests for each feature.

## Priority Order

1. Or-patterns (simplest — single function change in parsePattern)
2. Labelled loops (small — label parsing in 3 loop parsers + break)
3. `if let` / `while let` (medium — new AST nodes + Sema + HIR desugar)
4. `let-else` (medium — LetStmt modification + Sema divergence check)
