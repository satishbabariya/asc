# Compiler Improvements — Phase 1 & 2

| Field | Value |
|---|---|
| Date | 2026-04-16 |
| Goal | Fix-it hints, LSP hover/definition, E009 warning, @copy lowering, channel completion |
| Baseline | 244/244 tests, ~80% overall RFC coverage |
| Target | RFC-0006: 82%→~88%, RFC-0010: 88%→~93%, RFC-0005: 80%→~85%, RFC-0007: 35%→~40%, overall: ~83% |

## Motivation

The std library is at its ceiling (~80% overall). All remaining coverage gains require compiler work (C++/MLIR). This spec covers the most achievable compiler improvements: easy wins (fix-it hints, LSP) and moderate correctness items (E009, @copy, channels).

## Phase 1: Easy Wins

### 1a. Fix-it Hints in Sema

**File:** `lib/Sema/SemaDecl.cpp`, `lib/Sema/SemaExpr.cpp`

The fix-it infrastructure exists at `lib/Basic/Diagnostic.cpp:26-30` — `addFixIt(SourceRange, StringRef replacement)` stores suggestions that render as `suggestion: replace with '<replacement>'` in human format. Zero call sites exist today.

**Add fix-it suggestions for:**
1. **Unused variable warning (W005)** — suggest prefixing with `_` (e.g., `let _x = ...`)
2. **Mutable variable never mutated** — suggest changing `let` to `const`
3. **Missing type annotation on function param** — suggest adding explicit type

Each fix-it is a one-line `.addFixIt(range, replacement)` call chained onto an existing diagnostic emission. No new diagnostics needed — just augmenting existing warnings/errors with actionable suggestions.

### 1b. LSP textDocument/definition

**File:** `lib/Driver/Driver.cpp:588-607`

Currently returns `{"result": null}`. The fix:
1. Parse the file and run Sema (reuse existing `runCheck` path which builds the AST + symbol table)
2. Walk the AST to find the node at the request position (line/col)
3. If the node is a name reference, look up its declaration via the symbol table
4. Return the declaration's source location as `{uri, range}`

The Sema name resolution already resolves all identifiers to their declarations, and every `Decl` node has a `loc` field with file/line/col. The work is plumbing — connecting the LSP request position to the existing symbol resolution.

### 1c. LSP textDocument/hover

**File:** `lib/Driver/Driver.cpp:534-586`

Currently returns `"asc: line N, col C"`. The fix:
1. Same AST lookup as definition (find node at position)
2. Extract the node's type from Sema (every Expr has a resolved type, every Decl has its declared type)
3. Format as markdown: `` `fn(i32, ref<String>) -> Vec<i32>` `` or `` `let x: own<Vec<i32>>` ``
4. Return as `{"contents": {"kind": "markdown", "value": "<type_string>"}}`

## Phase 2: Correctness

### 2a. E009 / W004 Unbounded Recursion Warning

**File:** `lib/Analysis/StackSizeAnalysis.cpp:86-87`

Currently, when a recursive call is detected (function already in `visited` set), the pass silently returns 0. Instead:
1. When `visited.insert(op).second` is false, emit a warning: `W004: potential unbounded recursion in function '<name>'`
2. Use the MLIR `op->emitWarning()` API with the function name
3. Still return 0 for the stack size estimate (conservative)

### 2b. @copy Aggregate Lowering

**File:** `lib/HIR/HIRBuilder.cpp:1101`

Currently a `break` with a TODO comment. The fix:
1. When visiting a parameter or assignment where the source type has `@copy` attribute and is an aggregate (struct), emit `own::OwnCopyOp` instead of `own::OwnMoveOp`
2. The `OwnCopyOp` builder pattern exists in `lib/HIR/OwnOps.cpp` — use it the same way `OwnMoveOp` is used
3. `OwnershipLowering.cpp` already handles `OwnCopyOp` → `memcpy` in codegen

### 2c. Channel .send()/.recv() Wiring

**File:** `lib/HIR/HIRBuilder.cpp:3249`

Currently "single-threaded stubs". The runtime functions already exist in `lib/Runtime/channel_rt.c`:
- `__asc_chan_make(capacity, elem_size) -> *void`
- `__asc_chan_send(chan, data_ptr) -> void`
- `__asc_chan_recv(chan, out_ptr) -> void`
- `__asc_chan_drop(chan) -> void`

The HIRBuilder already emits inline channel code at lines 4690-4870 for `chan<T>(n)` syntax. The `.send()` and `.recv()` method calls need to:
1. Look up the channel variable (already resolved by Sema)
2. Emit a call to `__asc_chan_send` / `__asc_chan_recv` with the channel pointer and data pointer
3. The ConcurrencyLowering pass at `lib/CodeGen/ConcurrencyLowering.cpp` already declares these runtime symbols

## Files Modified

**Phase 1:**
- `lib/Sema/SemaDecl.cpp` — add fix-it hints to unused variable warnings
- `lib/Sema/SemaExpr.cpp` — add fix-it hints to type mismatch errors
- `lib/Driver/Driver.cpp` — LSP definition + hover handlers

**Phase 2:**
- `lib/Analysis/StackSizeAnalysis.cpp` — E009/W004 recursion warning
- `lib/HIR/HIRBuilder.cpp` — @copy aggregate lowering + channel method wiring

## Testing

- All 244 existing lit tests must continue to pass
- New tests for each feature:
  - `test/e2e/fixit_unused_prefix.ts` — verify fix-it suggestion appears in output
  - `test/e2e/lsp_definition.ts` — verify LSP returns non-null for known symbol
  - `test/e2e/lsp_hover_type.ts` — verify LSP returns type string
  - `test/e2e/recursion_warning.ts` — verify W004 emitted for recursive function
  - `test/e2e/copy_aggregate.ts` — verify @copy struct passes by value correctly

## What This Does NOT Include

- Drop flags for conditional moves (needs runtime flag infrastructure)
- Multi-module linking (needs cross-module IR resolution)
- E010-E012 (depends on drop flag infrastructure)
- fold/map/filter callback lowering (depends on closure improvements)
- Wasm EH proposal (large scope, separate spec)
- DWARF-in-Wasm debug sections
- Source maps
