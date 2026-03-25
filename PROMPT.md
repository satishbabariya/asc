# ASC Compiler — Autonomous Implementation Prompt

> **Paste this entire prompt into Claude Code Web to begin autonomous implementation.**
> **Estimated scope: full compiler from lexer to Wasm output.**

---

## Instructions for Claude

You are implementing **asc**, an AssemblyScript compiler built on LLVM/MLIR in **C++**. The repository contains accepted RFCs in `rfcs/` that are the **sole source of truth** for all design decisions. Read them before writing any code.

**Rules:**
- Do NOT stop and ask questions. If something is ambiguous, make the best decision consistent with the RFCs and document it in a comment.
- Do NOT summarize what you're about to do. Just do it.
- After completing each phase, immediately begin the next phase. Do not wait for confirmation.
- If you hit a tool limit, pick up exactly where you left off.
- Commit after each phase with a descriptive message.
- Write production-quality C++ (C++20, clang-format, no warnings with `-Wall -Wextra`).
- Follow the LLVM/Clang coding style: `CamelCase` for types, `camelCase` for functions/variables, `UPPER_CASE` for macros.
- Use LLVM's ADT types (`StringRef`, `SmallVector`, `ArrayRef`, `DenseMap`) over STL equivalents.
- Every component must have unit tests using `gtest`.

---

## Project Context (from RFCs — do NOT re-read RFCs unless you need specific details)

### What this compiler does
- Compiles AssemblyScript (TypeScript subset) to WebAssembly and all LLVM targets
- Uses MLIR as the HIR layer with two custom dialects: `own` (ownership) and `task` (concurrency)
- Enforces Rust-inspired ownership/borrowing at compile time — no garbage collector
- Primary target: `wasm32-wasi-threads`

### Six-stage pipeline (RFC-0003)
```
Source (.ts) → Lexer → Parser → AST → Sema → HIR (MLIR) → LLVM IR → Target
```

### Key design decisions
- No Binaryen — LLVM Wasm backend directly
- No GC — compile-time ownership only
- MLIR as HIR — custom dialects, not custom IR
- TypeScript-compatible syntax — ownership annotations only at boundaries
- Concurrency via ownership-based Send/Sync + channels

### Ownership types (RFC-0005)
- `!own.val<T>` — owned value, exactly one consuming use (linear)
- `!borrow<T>` — shared borrow, scoped to a region
- `!borrow.mut<T>` — exclusive mutable borrow, scoped to a region

### Surface syntax ownership annotations (RFC-0002, RFC-0015)
- `own<T>` — sole owner
- `ref<T>` — shared borrow (inferred inside bodies)
- `refmut<T>` — exclusive mutable borrow (inferred inside bodies)
- Annotations required only at function boundaries when inference is ambiguous

### Borrow checker — 5 passes on MLIR HIR (RFC-0006)
1. Liveness analysis (backward dataflow)
2. Region inference (NLL algorithm)
3. Aliasing constraint check (Rules A, B, C)
4. Move validity check (linearity + conditional moves)
5. Send/Sync check (task.spawn capture validation)

### Post-borrow-checker transforms
6. Drop insertion (RFC-0008) — `own.drop` at every scope exit, LIFO order, drop flags for conditional moves
7. Panic scope wrapping (RFC-0009) — Wasm EH `try`/`catch` for deterministic drops on unwind

### MLIR dialect ops (RFC-0005)
- `own.alloc<T>`, `own.move<T>`, `own.drop<T>`, `own.copy<T>`
- `borrow.ref<T>`, `borrow.mut<T>`
- `task.spawn`, `task.join`, `chan.make`, `chan.send`, `chan.recv`

### Lowering to LLVM IR (RFC-0003)
- `own.alloc` → `alloca` (stack) or `malloc` (heap, via escape analysis)
- `own.move` → SSA forwarding (scalar) or `memcpy` (aggregate)
- `own.drop` → destructor call + `free` (heap only)
- `borrow.ref`/`borrow.mut` → raw pointer (`getelementptr`)
- `task.spawn` → closure struct + `wasi_thread_start` (Wasm) / `pthread_create` (native)

### Primitive types (RFC-0015)
`i8`, `i16`, `i32`, `i64`, `i128`, `u8`, `u16`, `u32`, `u64`, `u128`, `f32`, `f64`, `bool`, `char`, `usize`, `isize`, `void`, `never`

### Token categories for Lexer
- Keywords: `const`, `let`, `function`, `fn`, `return`, `if`, `else`, `while`, `loop`, `for`, `of`, `break`, `continue`, `match`, `struct`, `enum`, `trait`, `impl`, `import`, `export`, `from`, `type`, `static`, `own`, `ref`, `refmut`, `task`, `chan`, `unsafe`, `dyn`, `where`, `as`, `true`, `false`, `null`
- Operators: `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `~`, `!`, `<`, `>`, `=`, `.`, `,`, `:`, `;`, `?`, `@`, `#`, `..`, `..=`, `=>`, `->`, `::`, `<<`, `>>`, `<=`, `>=`, `==`, `!=`, `&&`, `||`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
- Delimiters: `(`, `)`, `[`, `]`, `{`, `}`
- Literals: integers (decimal/hex/octal/binary with suffixes), floats, strings, chars, template literals, raw strings
- Comments: `//`, `/* */`, `///`, `/** */`

### Precedence table (RFC-0015, highest to lowest)
15: `.` `[]` method call | 14: unary `-!~&*` `as` | 13: `*/%` | 12: `+-` | 11: `<<>>` | 10: `&` | 9: `^` | 8: `|` | 7: `.. ..=` | 6: `== != < > <= >=` | 5: `&&` | 4: `||` | 3: `?` | 2: `= += -= *= /= %= &= |= ^= <<= >>=` | 1: `return break continue =>`

### LLVM/Clang reference files to mirror
| Stage | Reference |
|---|---|
| Lexer | `clang/lib/Lex/Lexer.cpp` |
| Parser | `clang/lib/Parse/ParseDecl.cpp`, `ParseExpr.cpp` |
| AST | `clang/include/clang/AST/Stmt.h`, `Decl.h`, `Expr.h` |
| Sema | `clang/lib/Sema/SemaDecl.cpp`, `SemaExpr.cpp` |
| Constant folding | `flang/lib/Evaluate/` |
| HIR builder | `flang/lib/Lower/` (FIR emission pattern) |
| MLIR dialect ops | `flang/lib/Optimizer/Dialect/FIROps.cpp` |
| Codegen | `clang/lib/CodeGen/CGExpr.cpp`, `CGDecl.cpp`, `CGCall.cpp` |
| Wasm ABI | `clang/lib/CodeGen/TargetInfo.cpp` → `WebAssemblyABIInfo` |
| Pass pipeline | `llvm/lib/Passes/PassBuilder.cpp` |

---

## Directory Structure

Create this structure at the start:

```
asc/
├── CMakeLists.txt                    # Top-level CMake (requires LLVM/MLIR)
├── include/
│   └── asc/
│       ├── Basic/
│       │   ├── Diagnostic.h          # DiagnosticEngine, error codes (RFC-0010)
│       │   ├── DiagnosticIDs.h       # E001–E010, W001–W002 registry
│       │   ├── SourceLocation.h      # (file-id, offset) pair
│       │   ├── SourceManager.h       # File buffer management
│       │   ├── TokenKinds.h          # Token enum
│       │   └── TokenKinds.def        # X-macro token definitions
│       ├── Lex/
│       │   ├── Lexer.h
│       │   └── Token.h
│       ├── Parse/
│       │   └── Parser.h
│       ├── AST/
│       │   ├── ASTContext.h          # Bump allocator, owns all nodes
│       │   ├── Decl.h               # FunctionDecl, VarDecl, TypeDecl, FieldDecl
│       │   ├── Stmt.h               # CompoundStmt, IfStmt, ReturnStmt, ...
│       │   ├── Expr.h               # CallExpr, DeclRefExpr, BinaryExpr, ...
│       │   ├── Type.h               # Type hierarchy + ownership annotations
│       │   └── ASTVisitor.h         # CRTP visitor
│       ├── Sema/
│       │   └── Sema.h               # Name resolution, type checking, ownership inference
│       ├── HIR/
│       │   ├── OwnDialect.h         # own dialect C++ declarations
│       │   ├── TaskDialect.h        # task dialect C++ declarations
│       │   ├── OwnOps.h             # own.alloc, own.move, own.drop, own.copy, borrow.*
│       │   ├── TaskOps.h            # task.spawn, task.join, chan.*
│       │   ├── OwnTypes.h           # !own.val<T>, !borrow<T>, !borrow.mut<T>
│       │   └── HIRBuilder.h         # AST → MLIR emission
│       ├── Analysis/
│       │   ├── LivenessAnalysis.h   # Pass 1
│       │   ├── RegionInference.h    # Pass 2
│       │   ├── AliasCheck.h         # Pass 3
│       │   ├── MoveCheck.h          # Pass 4
│       │   ├── SendSyncCheck.h      # Pass 5
│       │   ├── DropInsertion.h      # Transform pass (RFC-0008)
│       │   └── PanicScopeWrap.h     # Transform pass (RFC-0009)
│       ├── CodeGen/
│       │   ├── OwnershipLowering.h  # own dialect → LLVM dialect
│       │   ├── ConcurrencyLowering.h # task dialect → platform primitives
│       │   └── CodeGen.h            # MLIR → LLVM IR → TargetMachine
│       └── Driver/
│           └── Driver.h             # CLI argument parsing
├── lib/
│   ├── Basic/
│   │   ├── Diagnostic.cpp
│   │   ├── SourceManager.cpp
│   │   └── CMakeLists.txt
│   ├── Lex/
│   │   ├── Lexer.cpp
│   │   └── CMakeLists.txt
│   ├── Parse/
│   │   ├── Parser.cpp
│   │   ├── ParseDecl.cpp
│   │   ├── ParseStmt.cpp
│   │   ├── ParseExpr.cpp
│   │   └── CMakeLists.txt
│   ├── AST/
│   │   ├── ASTContext.cpp
│   │   ├── Type.cpp
│   │   └── CMakeLists.txt
│   ├── Sema/
│   │   ├── Sema.cpp
│   │   ├── SemaDecl.cpp
│   │   ├── SemaExpr.cpp
│   │   ├── SemaType.cpp
│   │   └── CMakeLists.txt
│   ├── HIR/
│   │   ├── OwnDialect.cpp
│   │   ├── OwnOps.cpp
│   │   ├── OwnTypes.cpp
│   │   ├── TaskDialect.cpp
│   │   ├── TaskOps.cpp
│   │   ├── HIRBuilder.cpp
│   │   ├── OwnDialect.td           # TableGen dialect definition
│   │   ├── OwnOps.td               # TableGen op definitions
│   │   ├── TaskDialect.td
│   │   ├── TaskOps.td
│   │   └── CMakeLists.txt
│   ├── Analysis/
│   │   ├── LivenessAnalysis.cpp
│   │   ├── RegionInference.cpp
│   │   ├── AliasCheck.cpp
│   │   ├── MoveCheck.cpp
│   │   ├── SendSyncCheck.cpp
│   │   ├── DropInsertion.cpp
│   │   ├── PanicScopeWrap.cpp
│   │   └── CMakeLists.txt
│   ├── CodeGen/
│   │   ├── OwnershipLowering.cpp
│   │   ├── ConcurrencyLowering.cpp
│   │   ├── CodeGen.cpp
│   │   └── CMakeLists.txt
│   └── Driver/
│       ├── Driver.cpp
│       └── CMakeLists.txt
├── tools/
│   └── asc/
│       ├── main.cpp                  # CLI entry point
│       └── CMakeLists.txt
├── test/
│   ├── Lex/                         # Lexer unit tests
│   ├── Parse/                       # Parser unit tests
│   ├── AST/                         # AST construction tests
│   ├── Sema/                        # Type checking tests
│   ├── HIR/                         # MLIR dialect tests
│   ├── Analysis/                    # Borrow checker tests
│   ├── CodeGen/                     # Lowering tests
│   └── lit.cfg.py                   # LLVM lit test config
├── unittests/
│   ├── Lex/
│   │   └── LexerTest.cpp
│   ├── Parse/
│   │   └── ParserTest.cpp
│   ├── AST/
│   │   └── ASTTest.cpp
│   ├── Sema/
│   │   └── SemaTest.cpp
│   └── CMakeLists.txt
└── CLAUDE.md
```

---

## Implementation Plan — Execute All Phases Sequentially

### Phase 1: Build System + Basic Infrastructure
**Read:** RFC-0001, RFC-0010 (CLI flags/exit codes)

1. Create `CMakeLists.txt` that finds LLVM and MLIR packages (`find_package(LLVM)`, `find_package(MLIR)`)
2. Set up all subdirectory `CMakeLists.txt` files
3. Implement `SourceLocation.h` — `(FileID, unsigned Offset)` pair, exactly like Clang's
4. Implement `SourceManager.h/.cpp` — file buffer ownership, `getFileID()`, `getLineAndColumn()`
5. Implement `Diagnostic.h/.cpp` — `DiagnosticEngine` with severity levels, source excerpts, caret display
6. Implement `DiagnosticIDs.h` — error code registry from RFC-0010: E001–E010, W001–W002
7. Implement `TokenKinds.h` + `TokenKinds.def` — X-macro pattern for all token kinds
8. Write unit tests for `SourceManager` and `DiagnosticEngine`
9. **Commit: "feat: build system and basic infrastructure"**

### Phase 2: Lexer
**Read:** RFC-0015 (complete syntax — all token types, literals, operators, keywords)

1. Implement `Token.h` — `Token` class with `TokenKind`, `SourceLocation`, `StringRef` spelling
2. Implement `Lexer.h/.cpp` — hand-written, non-backtracking, Clang-style:
   - UTF-8 source input (re-encode to UTF-16 for string values where needed)
   - All keywords from RFC-0015 (including `own`, `ref`, `refmut`, `task`, `chan`, `struct`, `enum`, `trait`, `impl`, `match`, `loop`, `dyn`, `where`, `fn`)
   - All operators including multi-char: `..`, `..=`, `=>`, `->`, `::`, `<<`, `>>`, `<=`, `>=`, `==`, `!=`, `&&`, `||`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
   - Integer literals with suffixes: `42i8`, `42u32`, `42i64`, `42usize`, hex `0xFF`, octal `0o77`, binary `0b1010`, underscore separators `1_000_000`
   - Float literals with suffixes: `3.14f32`, `3.14` (default f64)
   - String literals: regular `"..."`, raw `r"..."`, raw delimited `r#"..."#`
   - Character literals: `'a'`, escape sequences `\n \t \\ \' \" \0`, Unicode `\u{1F600}`
   - Template literals: `` `Hello ${expr}` `` — tokenize as sequence of string parts + expressions
   - Comments: `//`, `/* */` (nested), `///` doc comments, `/** */` block doc
   - Attributes: `@copy`, `@send`, `@sync`, `@heap`, `@inline`, `@cold`, `@test`, `@repr(...)`, etc.
   - Discard all whitespace and comment tokens (not stored)
   - Source location attached to every token
3. Write comprehensive lexer tests covering all token types, edge cases, error recovery
4. **Commit: "feat: hand-written lexer with full token support"**

### Phase 3: AST Node Definitions
**Read:** RFC-0003 (AST hierarchy), RFC-0015 (all syntax constructs)

1. Implement `ASTContext.h/.cpp` — bump allocator (`llvm::BumpPtrAllocator`) that owns all nodes
2. Implement `Type.h/.cpp` — type hierarchy:
   - `BuiltinType` (i8–i128, u8–u128, f32, f64, bool, char, usize, isize, void, never)
   - `OwnType`, `RefType`, `RefMutType` (ownership wrappers)
   - `NamedType`, `GenericType`, `SliceType`, `ArrayType`, `TupleType`, `FnType`, `DynTraitType`
   - `NullableType` (T | null)
3. Implement `Decl.h` — declaration hierarchy:
   - `FunctionDecl` (name, params, return type, generic params, where clause, body, attributes)
   - `VarDecl` (const/let, pattern, type annotation, initializer, ownership annotation)
   - `StructDecl` (name, generic params, fields, attributes: @copy, @send, @sync, @repr)
   - `EnumDecl` (name, generic params, variants)
   - `TraitDecl` (name, generic params, supertraits, items)
   - `ImplDecl` (generic params, type, trait, items)
   - `TypeAliasDecl`, `ConstDecl`, `StaticDecl`
   - `ImportDecl`, `ExportDecl`
   - `FieldDecl` (name, type)
4. Implement `Stmt.h` — statement hierarchy:
   - `CompoundStmt` (block: `{ stmts }`)
   - `LetStmt`, `ConstStmt`
   - `ExprStmt`
   - `ReturnStmt`, `BreakStmt`, `ContinueStmt`
5. Implement `Expr.h` — expression hierarchy:
   - `IntegerLiteral`, `FloatLiteral`, `StringLiteral`, `CharLiteral`, `BoolLiteral`, `NullLiteral`
   - `ArrayLiteral`, `ArrayRepeatExpr`, `StructLiteral`, `TupleLiteral`
   - `DeclRefExpr` (variable/function reference)
   - `BinaryExpr` (all operators from precedence table)
   - `UnaryExpr` (`-`, `!`, `~`, `&`, `*`)
   - `CallExpr`, `MethodCallExpr`
   - `FieldAccessExpr`, `IndexExpr`
   - `CastExpr` (`as`)
   - `RangeExpr` (`..`, `..=`)
   - `IfExpr` (expression form), `MatchExpr`, `LoopExpr`, `WhileExpr`, `ForExpr`
   - `ClosureExpr` (arrow function)
   - `AssignExpr` (including compound: `+=`, etc.)
   - `BlockExpr`
   - `MacroCallExpr` (`format!`, `println!`, `panic!`, `assert!`, etc.)
   - `UnsafeBlockExpr`
   - `TemplateLiteralExpr`
   - `TryExpr` (`?` operator)
   - `PathExpr` (`Foo::Bar::baz`)
6. Implement `ASTVisitor.h` — CRTP visitor pattern for all node types
7. Implement pattern nodes for `match`/`if let`/`while let`:
   - `LiteralPattern`, `IdentPattern`, `TuplePattern`, `StructPattern`, `EnumPattern`
   - `SlicePattern`, `RangePattern`, `WildcardPattern`, `OrPattern`, `GuardPattern`
8. Write AST construction tests
9. **Commit: "feat: complete AST node hierarchy"**

### Phase 4: Parser
**Read:** RFC-0015 (complete EBNF grammar, precedence table)

1. Implement `Parser.h` — recursive descent parser, no backtracking:
   - `Parser(Lexer &lexer, ASTContext &ctx, DiagnosticEngine &diags)`
   - Main entry: `parseProgram()` → vector of `Item*`
2. Implement `ParseDecl.cpp`:
   - `parseFunctionDef()` — attributes, generic params, param list, return type, body
   - `parseStructDef()` — attributes (@copy, @send, @sync, @repr), fields
   - `parseEnumDef()` — C-like and algebraic variants
   - `parseTraitDef()` — supertraits, associated types/constants, default methods
   - `parseImplBlock()` — `impl Type` and `impl Trait for Type`
   - `parseImportDecl()` — `import { X, Y } from 'path'`
   - `parseExportDecl()`
   - `parseTypeAlias()`, `parseConstDef()`, `parseStaticDef()`
3. Implement `ParseStmt.cpp`:
   - `parseBlock()` — `{ stmt* expr? }`
   - `parseLetStmt()`, `parseConstStmt()`
   - Statement vs expression disambiguation
4. Implement `ParseExpr.cpp`:
   - Pratt parser for binary expressions using the 15-level precedence table
   - `parseUnaryExpr()`, `parsePrimaryExpr()`
   - `parseCallExpr()`, `parseMethodCallExpr()`
   - `parseIfExpr()`, `parseMatchExpr()`, `parseLoopExpr()`, `parseWhileExpr()`, `parseForExpr()`
   - `parseClosureExpr()` — `(params) => expr` and `(params) => { block }`
   - `parseTemplateExpr()` — template literal interpolation
   - `parseMacroCallExpr()` — `name!(args)`
   - Pattern parsing for `match` arms, `if let`, `while let`, `let-else`
5. Implement type parsing:
   - `parseType()` — primitives, named, `own<T>`, `ref<T>`, `refmut<T>`, `[T]`, `[T; N]`, tuples, `dyn Trait`, `T | null`
   - `parseGenericParams()`, `parseGenericArgs()`, `parseWhere()`
6. Error recovery — Clang-style:
   - On unexpected token, emit diagnostic
   - Skip to sync point: `;`, `}`, `{`, or declaration keyword
   - Continue parsing, report multiple errors per file
7. Write parser tests for every construct in the grammar
8. **Commit: "feat: recursive descent parser with full grammar support"**

### Phase 5: Semantic Analysis (Sema)
**Read:** RFC-0002 (ownership inference rules), RFC-0003 (Sema responsibilities), RFC-0015 (unsupported features list)

1. Implement scope/symbol table:
   - Lexical scope chain for name resolution
   - Symbol table entries with type + ownership annotation
2. Implement `SemaDecl.cpp`:
   - Resolve names through scope chains
   - Register function/struct/enum/trait/impl declarations
   - Check trait implementations satisfy all required methods
   - Validate `@copy` (all fields Copy, no Drop impl)
   - Validate `@send`/`@sync` (field-by-field check)
3. Implement `SemaExpr.cpp`:
   - Type inference for all expressions
   - Overload resolution (TypeScript-compatible order)
   - Infer and attach ownership types to all AST nodes per RFC-0002 rules:
     - Value never used after call → moved
     - Value passed to non-consuming function → borrowed
     - Value returned → transfers ownership
     - Conditional move → drop flag
4. Implement `SemaType.cpp`:
   - Type checking and compatibility
   - Generic instantiation and monomorphization
   - Trait bound checking
   - Reject unsupported TypeScript features (RFC-0015 §21): `any`, `unknown`, union types beyond `T | null`, structural typing, `class` inheritance, `interface`, `async/await`, `try/catch`, etc.
5. Implement constant folding (following `flang/lib/Evaluate/`):
   - Fold compile-time constants in Sema
   - Store as `ConstantExpr` nodes
6. Emit structured diagnostics with fix-it hints
7. Write Sema tests: type errors, ownership inference, unsupported feature rejection
8. **Commit: "feat: semantic analysis with ownership inference"**

### Phase 6: MLIR Dialects (own + task)
**Read:** RFC-0005 (dialect definitions, TableGen, type system, verifiers)

1. Write `OwnDialect.td` — TableGen dialect definition:
   ```tablegen
   def OwnDialect : Dialect {
     let name = "own";
     let cppNamespace = "::asc::own";
     let dependentDialects = ["mlir::arith::ArithDialect", "mlir::memref::MemRefDialect"];
   }
   ```
2. Write `OwnOps.td` — TableGen op definitions:
   - `own.alloc` — `() -> !own.val<T>`
   - `own.move` — `(!own.val<T>) -> !own.val<T>`
   - `own.drop` — `(!own.val<T>) -> ()`
   - `own.copy` — `(!own.val<T>) -> (!own.val<T>, !own.val<T>)` (requires @copy)
   - `borrow.ref` — `(!own.val<T>, region) -> !borrow<T>`
   - `borrow.mut` — `(!own.val<T>, region) -> !borrow.mut<T>`
3. Write `TaskDialect.td` + `TaskOps.td`:
   - `task.spawn` — `(!own.val<T>...) -> !task.handle`
   - `task.join` — `(!task.handle) -> !own.val<R>`
   - `chan.make` — `(capacity: i32) -> (!chan.tx<T>, !chan.rx<T>)`
   - `chan.send` — `(!chan.tx<T>, !own.val<T>) -> ()`
   - `chan.recv` — `(!chan.rx<T>) -> !own.val<T>`
4. Implement custom types:
   - `OwnValType` with `inner`, `isSend`, `isSync` parameters
   - `BorrowType`, `BorrowMutType` with region token
   - `TaskHandleType`, `ChanTxType`, `ChanRxType`
5. Implement op verifiers (RFC-0005 verifier table):
   - `own.alloc`: result must be `!own.val<T>`
   - `own.move`: operand `!own.val<T>`, no other uses
   - `own.drop`: operand `!own.val<T>`, sole use
   - `own.copy`: operand type must have `@copy`
   - `borrow.ref`: region token dominates all uses
   - `borrow.mut`: no overlapping borrows on same source
   - `task.spawn`: all captured `!own.val` have `send=true`
   - `chan.send`: consumed `!own.val<T>` matches channel type
6. Implement linearity enforcement verifier — every `!own.val` has exactly one consuming use
7. Write MLIR dialect tests using `mlir-opt` round-trip patterns
8. **Commit: "feat: own and task MLIR dialects with verifiers"**

### Phase 7: HIR Builder (AST → MLIR)
**Read:** RFC-0003 (HIR builder rules), RFC-0005 (op semantics)

1. Implement `HIRBuilder.h/.cpp` — walks typed AST, emits MLIR:
   - Each function body → `mlir::func::FuncOp` with region of `own`/`task` ops
   - `new T()` / local declaration → `own.alloc<T>`
   - Function call consuming argument → `own.move` before call
   - Function call borrowing argument → `borrow.ref` or `borrow.mut` scoped to call
   - `return` → `own.move` of return value
   - `task.spawn(closure)` → `task.spawn` with captured `own.val` operands listed
   - `chan<T>(n)` → `chan.make`
   - `tx.send(v)` → `chan.send` consuming `own.val`
   - `rx.recv()` → `chan.recv` producing `own.val`
   - Pattern matching → branching with `own.move` in each arm
   - `if let` / `while let` → conditional `own.move`
2. Handle all expression types: binary ops → `arith` dialect ops, control flow → `cf` or `scf` dialect
3. Handle closures: capture analysis, closure struct type generation
4. Write HIR builder tests
5. **Commit: "feat: HIR builder — AST to MLIR emission"**

### Phase 8: Borrow Checker (5 Analysis Passes)
**Read:** RFC-0006 (all 5 passes in detail)

1. Implement `LivenessAnalysis.cpp` — Pass 1:
   - Backward dataflow: `live-in(B) = use(B) ∪ (live-out(B) − def(B))`
   - Output: `LivenessInfo` per function
   - Use MLIR's `mlir/Analysis/DataFlow/` framework
2. Implement `RegionInference.cpp` — Pass 2:
   - NLL algorithm from Rust RFC 2094
   - Compute minimal region span for each `borrow.ref`/`borrow.mut`
   - Propagate outlives constraints via union-find
   - Output: `RegionMap` mapping each borrow op to its solved region
3. Implement `AliasCheck.cpp` — Pass 3:
   - Rule A: at most one `borrow.mut` at any program point per source value
   - Rule B: `borrow.mut` excludes all `borrow.ref` on same source
   - Rule C: no `own.drop` while any borrow is live
   - Emit RFC-0010 diagnostics: E001 (mut while shared), E002 (shared while mut), E003 (borrow outlives)
4. Implement `MoveCheck.cpp` — Pass 4:
   - Simple use-after-move: `!own.val` with more than one consuming use → E004
   - Conditional move detection: value moved in one branch but not other → W001, mark for drop flag
   - E005: value moved in conditional branch
5. Implement `SendSyncCheck.cpp` — Pass 5:
   - Verify `task.spawn` captures: all `!own.val` must have `send=true`
   - `!borrow<T>` is Send iff T is Sync
   - `!borrow.mut<T>` is never Send
   - Emit E006 (non-Send captured), E008 (non-Send channel element)
6. All passes are read-only analyses — emit diagnostics, do not transform IR
7. If any diagnostic emitted in passes 1–5, compilation halts
8. Write borrow checker tests with expected-error patterns
9. **Commit: "feat: five-pass borrow checker on MLIR HIR"**

### Phase 9: Drop Insertion + Panic Scope Wrapping
**Read:** RFC-0008 (drop insertion algorithm, drop ordering, drop flags), RFC-0009 (panic scope wrapping, Wasm EH)

1. Implement `DropInsertion.cpp` — transform pass:
   - For each block exit, compute values live-in but not live-out
   - Insert `own.drop` in reverse declaration order (LIFO)
   - For conditional moves (marked by Pass 4): allocate `i1` drop flag, emit `if drop_flag { own.drop(V) }`
2. Implement `PanicScopeWrap.cpp` — transform pass:
   - Wrap every scope with live `!own.val` in Wasm `try`/`catch` block
   - Catch handler drops values in same LIFO order with drop flag checks
   - `rethrow` after catch handler drops
   - Skip wrapping for scopes with only `@copy` values (hot loop optimization)
   - Double-panic detection: `in_unwind` flag, `unreachable` trap on double panic
3. Write tests for drop ordering, conditional drops, panic unwind
4. **Commit: "feat: drop insertion and panic scope wrapping transforms"**

### Phase 10: LLVM IR Codegen (Lowering Passes)
**Read:** RFC-0003 (lowering tables), RFC-0004 (target matrix, concurrency per target), RFC-0007 (concurrency lowering detail)

1. Implement `OwnershipLowering.cpp` — MLIR conversion pass:
   - `own.alloc<T>` (stack) → `llvm.alloca sizeof(T) align alignof(T)`
   - `own.alloc<T>` (heap) → `llvm.call @malloc(sizeof(T))`
   - `own.drop<T>` → destructor call (if any) + `llvm.call @free(ptr)` (heap only)
   - `own.move<T>` (scalar) → SSA value forwarding
   - `own.move<T>` (aggregate) → `llvm.memcpy dst src sizeof(T)`
   - `own.copy<T>` → `llvm.memcpy` + new `own.alloc`
   - `borrow.ref<T>` → `llvm.getelementptr` (raw pointer)
   - `borrow.mut<T>` → `llvm.getelementptr` (raw mutable pointer)
   - Escape analysis to decide stack vs heap for `own.alloc`
2. Implement `ConcurrencyLowering.cpp` — switch on target triple:
   - **wasm32-wasi-threads:**
     - `task.spawn` → closure struct materialization + `wasi_thread_start` import call
     - `task.join` → `i32.atomic.wait` + `memory.copy` result
     - `chan.send` → ring buffer write (RFC-0007 Wasm instruction sequence)
     - `chan.recv` → ring buffer read (RFC-0007 Wasm instruction sequence)
     - `mutex.lock` → `i32.atomic.rmw.cmpxchg` + `i32.atomic.wait`
   - **POSIX (x86_64, aarch64, riscv64):**
     - `task.spawn` → `pthread_create`
     - `task.join` → `pthread_join`
     - Channel ops → C11 atomics + futex/semaphore
     - Mutex → `pthread_mutex_lock/unlock`
   - **Windows (x86_64-pc-windows-msvc):**
     - `task.spawn` → `CreateThread`
     - `task.join` → `WaitForSingleObject`
     - Mutex → `AcquireSRWLockExclusive`
   - Static stack size analysis for `task.spawn` (call graph walk)
3. Implement `CodeGen.cpp`:
   - Standard `mlir::LLVMConversionTarget` for remaining MLIR ops
   - `mlir::translateModuleToLLVMIR` → `llvm::Module`
   - Configure `llvm::PassBuilder` per optimization level:
     - O0: `mem2reg` only
     - O2: `mem2reg`, SROA, early-cse, inlining, GVN, instcombine, simplifycfg, loop-unroll, licm, stack-coloring
     - Oz: O2 + global-dce, mergefunc, strip-dead-prototypes
   - Create `TargetMachine` from target triple
   - Emit: `.wasm`, `.wat`, `.ll` (LLVM IR text), `.mlir`, `.o`, `.s` per `--emit` flag
   - DWARF debug info emission (native) and DWARF-in-Wasm sections (Wasm) when `--debug`
4. No-GC verification pass: scan final LLVM IR for `llvm.gcroot`/`llvm.gcwrite`/`llvm.gcread` → internal error if found
5. Write codegen tests
6. **Commit: "feat: ownership and concurrency lowering to LLVM IR"**

### Phase 11: CLI Driver
**Read:** RFC-0010 (CLI commands, flags, exit codes, error formats)

1. Implement `Driver.cpp`:
   - Parse CLI args using LLVM's `cl::opt`
   - Subcommands: `build`, `check`, `fmt`, `doc`, `lsp`
   - Flags: `--target`, `--emit`, `--opt`, `--debug`, `--max-threads`, `--error-format`, `--verbose`, `-o`
   - Exit codes: 0 success, 1 compile error, 2 ICE, 3 invalid args
2. Implement `main.cpp`:
   - Wire together: Source → Lexer → Parser → AST → Sema → HIR → BorrowCheck → DropInsert → PanicWrap → Lowering → Codegen → Output
   - `asc build`: full pipeline
   - `asc check`: pipeline up to borrow checker, no codegen
3. Implement `--error-format` output modes:
   - `human`: caret diagnostics with source excerpts
   - `json`: LSP-compatible JSON diagnostics
   - `github-actions`: `::error file=...,line=...,col=...::message`
4. Implement `--verbose`: print pipeline stage timings
5. **Commit: "feat: asc CLI driver with build and check commands"**

### Phase 12: End-to-End Test Suite
1. Create integration tests that compile `.ts` source through the full pipeline:
   - Simple function → Wasm output
   - Ownership move/borrow → verify borrow checker passes
   - Expected borrow checker errors (use-after-move, double borrow)
   - Concurrency: `task.spawn` + `chan` → verify Send/Sync checking
   - Conditional moves → drop flag insertion
   - Panic → verify Wasm EH wrapping
2. Create snapshot tests for `--emit mlir` and `--emit llvmir` outputs
3. Create lit tests following LLVM's test infrastructure
4. **Commit: "feat: end-to-end test suite"**

### Phase 13: Polish
1. Ensure all `CMakeLists.txt` link correctly
2. Add a top-level `README.md` build instructions section
3. Verify `asc build hello.ts` produces a valid `.wasm` for a simple program:
   ```typescript
   function main(): i32 {
     const x: i32 = 42;
     return x;
   }
   ```
4. **Commit: "feat: initial working compiler"**

---

## DO NOT STOP

After completing each phase, immediately proceed to the next. If you reach a tool usage limit, state "Resuming at Phase N, Step M" and continue from exactly that point when invoked again. The goal is a working compiler that can compile simple AssemblyScript programs to Wasm through the full pipeline.

If any phase requires a design decision not covered by the RFCs, make the simplest correct choice, leave a `// DECISION:` comment explaining what you chose and why, and keep moving.
