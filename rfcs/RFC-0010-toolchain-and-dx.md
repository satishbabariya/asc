# RFC-0010 — Toolchain and Developer Experience

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0001 through RFC-0009 |
| Clang reference | `clang/lib/Driver/`, `clang/lib/CodeGen/CGDebugInfo.cpp` |

## Summary

This RFC defines the compiler's user-facing interface: the `asc` command-line tool, error
message quality standards, DWARF debug info emission for native targets, source map
generation for Wasm, the module system, and the Language Server Protocol integration.

## CLI — `asc` Command

### Subcommands

| Command | Description |
|---|---|
| `asc build <file>` | Compile a source file. Outputs `.wasm` by default. |
| `asc check <file>` | Run frontend + borrow checker only. No codegen. Fast feedback. |
| `asc fmt <file>` | Format source file in place (built-in formatter). |
| `asc doc <file>` | Extract doc comments and emit JSON or HTML documentation. |
| `asc lsp` | Start Language Server Protocol server (stdin/stdout JSON-RPC). |

### Key Flags

| Flag | Default | Description |
|---|---|---|
| `--target <triple>` | `wasm32-wasi-threads` | LLVM target triple |
| `--opt <level>` | `2` | Optimization level: `0`, `1`, `2`, `3`, `s`, `z` |
| `--opt-size` | off | Alias for `--opt z` |
| `--emit <format>` | `wasm` | Output format: `wasm`, `wat`, `llvmir`, `mlir`, `obj`, `asm` |
| `--debug` | off | Emit DWARF (native) or Wasm DWARF + source maps (Wasm) |
| `--max-threads <N>` | `8` | Maximum concurrent tasks for thread stack arena sizing |
| `--error-format <fmt>` | `human` | Diagnostic format: `human`, `json`, `github-actions` |
| `--check-only` | off | Alias for `asc check` |
| `--no-panic-unwind` | off | Disable Wasm EH wrapping; panics trap immediately (no drops on unwind) |
| `-o <file>` | derived | Output file path |
| `--verbose` | off | Print pipeline stages and timings |

### Exit Codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | Compile error (diagnostic emitted) |
| `2` | Internal compiler error (bug) |
| `3` | Invalid CLI arguments |

### Examples

```sh
# Build for Wasm with default settings
asc build src/main.ts

# Type-check and borrow-check only (no codegen) — fast CI check
asc check src/main.ts

# Native Linux x86-64 build with debug info
asc build src/main.ts --target x86_64-unknown-linux-gnu --debug -o out/main

# Size-optimized Wasm
asc build src/main.ts --opt-size -o out/main.wasm

# Emit human-readable Wasm text format
asc build src/main.ts --emit wat -o out/main.wat

# Emit MLIR HIR for debugging (after borrow checking, before lowering)
asc build src/main.ts --emit mlir -o out/main.mlir

# JSON diagnostics for editor integration
asc check src/main.ts --error-format json
```

## Diagnostic Quality Standards

Every diagnostic must satisfy:

1. **Primary message** — one sentence, active voice, names the specific construct
2. **Source excerpt** — file path, line number, column number, source text, caret (`^`)
   pointing to the exact token or span
3. **At least one note** — points to the related location (e.g., where the conflicting
   borrow began, where a value was first moved)
4. **Optional suggestion** — a fix-it hint with concrete replacement text, emitted only
   when the fix is unambiguous

### Human Format Example

```
error[E001]: cannot borrow `data` as mutable because it is also borrowed as shared
  --> src/pipeline.ts:14:5
   |
12 |   const view = data.slice(0, 8);
   |                ---- shared borrow of `data` begins here
14 |   data.write(newBytes);
   |   ^^^^ mutable borrow occurs while shared borrow is active
   |
note: shared borrow of `data` ends at line 18
help: end the shared borrow before taking a mutable borrow
```

### JSON Format (LSP-compatible)

```json
{
  "severity": "error",
  "code": "E001",
  "message": "cannot borrow `data` as mutable because it is also borrowed as shared",
  "range": { "start": { "line": 13, "character": 4 }, "end": { "line": 13, "character": 8 } },
  "relatedInformation": [
    {
      "message": "shared borrow of `data` begins here",
      "location": { "uri": "src/pipeline.ts", "range": { "start": { "line": 11, "character": 15 }, "end": { "line": 11, "character": 19 } } }
    }
  ]
}
```

### Error Code Registry

| Code | Category | Description |
|---|---|---|
| E001 | Borrow | Mutable borrow while shared borrow active |
| E002 | Borrow | Shared borrow while mutable borrow active |
| E003 | Borrow | Borrow outlives owned value |
| E004 | Move | Use of value after move |
| E005 | Move | Value moved in conditional branch |
| E006 | Send | Non-Send type captured in task.spawn |
| E007 | Type | Missing `@copy` on bitwise-copied type |
| E008 | Type | Non-Send type used as channel element |
| E009 | Stack | Task body has unbounded recursion (no static stack size) |
| E010 | Panic | Double-panic detected in destructor |
| W001 | Move | Conditional move — drop flag inserted |
| W002 | Perf | Large `@copy` type; consider using `own<T>` |

## DWARF Debug Info (Native Targets)

On native targets (`--debug`), the compiler emits DWARF v5 debug information using
LLVM's `DIBuilder` API, following the same pattern as Clang's `CGDebugInfo.cpp`.

### What is emitted

- All function definitions with parameter names, types, and source locations
- All local variables including owned values (shown as their concrete type `T`, not
  `!own.val<T>` — ownership is invisible to the debugger)
- Borrow regions shown as lexical scopes (`DW_TAG_lexical_block`) in the DWARF scope tree
- Inline functions marked with `DW_AT_inline`
- Ownership drop points annotated as `DW_TAG_label` for step-through debugging

### Debugger experience

With DWARF debug info, users can:

- Set breakpoints at any source line
- Inspect owned values in the watch window as their concrete type
- Step into/over drop calls (shown as destructor calls)
- Observe the state of owned values at scope exit (just before drop)

## Source Maps (Wasm Target)

On the Wasm target with `--debug`, two outputs are emitted:

### DWARF-in-Wasm section

Following the [DWARF for WebAssembly specification](https://yurydelendik.github.io/webassembly-dwarf/),
DWARF data is embedded in custom sections of the `.wasm` file:
`.debug_info`, `.debug_abbrev`, `.debug_line`, `.debug_str`.

This enables source-level debugging in:
- Wasmtime (`wasmtime run --debug`)
- Chrome DevTools (DWARF extension)
- Firefox DevTools

### JSON source map

A `main.wasm.map` file is emitted alongside `main.wasm`, following the
[Source Map v3 specification](https://sourcemaps.info/spec.html). This is for JavaScript
tooling that wraps the Wasm module and needs to report errors in terms of `.ts` source
lines.

## Language Server Protocol

`asc lsp` starts an LSP server on stdin/stdout (JSON-RPC 2.0).

### Supported LSP capabilities

| Capability | Implementation |
|---|---|
| `textDocument/publishDiagnostics` | Runs `asc check` incrementally on file save |
| `textDocument/definition` | Resolves through ownership types to original declaration |
| `textDocument/hover` | Shows inferred ownership type for any expression |
| `textDocument/completion` | Context-aware; filtered by ownership (e.g., only `chan.tx` methods on a `tx` value) |
| `textDocument/codeAction` | Fix-it hints from diagnostics as quick-fix actions |
| `textDocument/formatting` | Delegates to `asc fmt` |
| `textDocument/inlayHints` | Shows inferred ownership annotations inline (opt-in) |

### Inlay hints example

```typescript
function transform(input: Buffer): Output {
  const result /*: own<Output>*/ = new Output();
  result.write(input /*: ref<Buffer>*/.slice(0, 8));
  return result; /*move*/
} /*drop input*/
```

Inlay hints show inferred ownership at every point, making the ownership model visible
without requiring explicit annotations.

## Module System

Each `.ts` source file is a module, which maps to a Wasm module (one `.wasm` per source
file, or a single linked `.wasm` for multi-file projects).

### Cross-module calls

Cross-module function calls use the Wasm import/export mechanism:

```typescript
// module a.ts — exports a function that takes ownership
export function process(data: own<Buffer>): own<Result> { ... }

// module b.ts — imports and calls it
import { process } from './a';
const result = process(myBuffer); // myBuffer moved into process
```

Ownership annotations at `export`/`import` boundaries are **required** — the compiler
cannot infer ownership for imported functions (no cross-module HIR). This is enforced by
Sema: any import with an `own<T>` parameter must have the annotation at the import site.

### Link-Time Optimization (LTO)

`asc build --emit bitcode` produces LLVM bitcode (`.bc`) for each module. Modules can be
linked with `llvm-link` and optimized with `opt` + `llc`, following Clang's ThinLTO
pipeline exactly. LTO allows cross-module inlining, cross-module escape analysis (enabling
stack allocation of values that escape into sibling modules), and cross-module dead code
elimination.

## Build System Integration

`asc` follows Unix conventions for build system integration:

- Exit code `0` = success, non-zero = failure
- All diagnostics to stderr, output file to stdout (or `-o`)
- `--error-format github-actions` emits `::error file=...,line=...,col=...::message`
  annotations for GitHub Actions CI
- Dependency file (`-MF <file>`) emits a Makefile-compatible `.d` file listing all
  transitive imports, for incremental build systems
