# RFC 100% Completion — Design Spec

**Date:** 2026-04-14
**Goal:** Bring all 20 RFCs from 78.5% to 100% implementation coverage
**Approach:** Bottom-up by dependency, layer-as-unit execution
**Baseline:** 199 tests passing, 17,688 C/C++ LOC, 9,334 std TS LOC

---

## Execution Strategy

Four layers executed sequentially. Each layer gets one spec, one implementation plan, one implementation cycle, and a validation gate. A layer must reach 100% before the next starts.

```
Layer 1: Compiler Correctness  (RFCs 0005–0009)  →  78.5% → ~88%
Layer 2: Compiler Completeness (RFCs 0001–0004, 0010, 0015)  →  ~88% → ~95%
Layer 3: Core Std Library      (RFCs 0011–0014)  →  ~95% → ~98%
Layer 4: Ecosystem Libraries   (RFCs 0016–0020)  →  ~98% → 100%
```

### Key Decisions

- **Wasm EH:** Keep setjmp/longjmp, document as implementation-defined. Wasm EH proposal is a future optimization, not a completeness gap.
- **MPMC Channels:** Mutex-guarded wrapper around SPSC. Correct by construction, avoids lock-free complexity.
- **Std Library Strategy:** Self-hosted — all std/ecosystem code is asc (TypeScript syntax) compiled by the asc compiler. Runtime C provides FFI foundation.

---

## Layer 1 — Compiler Correctness (RFCs 0005–0009)

**Prerequisite:** None (foundation layer)
**Target:** ~215 tests

### 1.1 Conditional Drop Branching (RFC-0008)

**Problem:** Drop flags emit alloca/store/load but no cf.cond_br. Drops execute unconditionally — double-free risk on conditional moves.

**Solution:** In OwnershipLowering.cpp, when processing own.drop with a drop_flag attribute:
1. Load the flag value (existing code)
2. Split current block into three: check-block, drop-block, merge-block
3. Emit `cf.cond_br(flagValue, dropBlock, mergeBlock)` in check-block
4. Move drop + free logic into drop-block, terminate with `cf.br(mergeBlock)`
5. merge-block continues with remaining operations

**Files:** lib/CodeGen/OwnershipLowering.cpp
**Tests:** Extend drop_flag_if_else.ts and drop_flag_match.ts to verify conditional branching in LLVM IR output.

### 1.2 Struct Literals Through Escape Analysis (RFC-0005)

**Problem:** `visitStructLiteral()` emits `llvm.alloca` directly, bypassing `own.alloc`. Escape analysis never sees struct allocations.

**Solution:** Refactor HIRBuilder::visitStructLiteral() to:
1. Emit `own.alloc` for the struct type
2. Store field values into the allocated struct
3. Let EscapeAnalysis classify as stack_safe or must_heap
4. OwnershipLowering lowers to alloca or malloc accordingly

**Files:** lib/HIR/HIRBuilder.cpp, potentially lib/Analysis/EscapeAnalysis.cpp
**Tests:** New test verifying struct returned from function triggers malloc. Existing escape_return.ts / escape_local.ts should continue passing.

### 1.3 Closure Capture Env Struct for task.spawn (RFC-0007)

**Problem:** Spawned tasks can't access parent scope variables. No capture mechanism exists.

**Solution:** New `buildClosureEnv()` in HIRBuilder:
1. When visiting task.spawn with a closure body, analyze free variables in the closure
2. Build a packed struct type containing each captured variable
3. Allocate the env struct on the heap (must outlive spawning scope)
4. Store captured values into the struct
5. Pass struct pointer as the `void* arg` to pthread_create
6. In the spawned function prologue, cast arg back and extract fields
7. Borrow checker (SendSyncCheck) validates all captures are Send

**Files:** lib/HIR/HIRBuilder.cpp, include/asc/HIR/HIRBuilder.h
**Tests:** New test spawning task that reads parent variable. Negative test: capturing non-Send type produces error.

### 1.4 MPMC Channels (RFC-0007)

**Problem:** Only SPSC ring buffer exists. Multiple producers/consumers not supported.

**Solution:** Add mutex-guarded MPMC in channel_rt.c:
1. `AscMPMCChannel` struct: buffer, head, tail, capacity, mutex, not_empty condvar, not_full condvar, ref_count
2. `__asc_mpmc_chan_create(capacity, elem_size)` — allocate channel + buffer
3. `__asc_mpmc_chan_send(chan, data)` — lock, wait if full, memcpy, signal not_empty, unlock
4. `__asc_mpmc_chan_recv(chan, out)` — lock, wait if empty, memcpy, signal not_full, unlock
5. `__asc_mpmc_chan_destroy(chan)` — atomic ref-count decrement, free on zero

Compiler emits MPMC variant when channel is used by multiple producers or consumers (detected at borrow-check time by counting distinct task.spawn blocks that reference tx/rx ends).

**Files:** lib/Runtime/channel_rt.c, lib/CodeGen/ConcurrencyLowering.cpp, lib/HIR/HIRBuilder.cpp
**Tests:** Multi-producer test (2 tasks sending, 1 receiving), multi-consumer test.

### 1.5 Scoped Threads (RFC-0007)

**Problem:** No lifetime-bounded spawn that can borrow from parent scope.

**Solution:**
1. New syntax: `task.scoped(|s| { s.spawn(fn_borrowing_parent); })` — scope block
2. HIRBuilder emits a scope region that joins all spawned threads before exiting
3. Borrow checker validates borrows don't outlive the scope block (existing region inference handles this)
4. Lowering: insert pthread_join for each spawned handle before scope exit

**Files:** lib/HIR/HIRBuilder.cpp, lib/CodeGen/ConcurrencyLowering.cpp
**Tests:** Scoped spawn reading parent ref. Negative: borrow escaping scope produces E002/E007.

### 1.6 Thread Arena Allocator (RFC-0008)

**Problem:** Wasm has bump allocator but no thread-local arena for native targets.

**Solution:** In runtime.c:
1. `__asc_arena_init(size)` — mmap/malloc a thread-local arena buffer
2. `__asc_arena_alloc(size, align)` — bump pointer allocation within arena
3. `__asc_arena_reset()` — reset bump pointer to start (bulk deallocation)
4. `__asc_arena_destroy()` — free the arena buffer
5. Thread-local storage: `_Thread_local` on native, static on Wasm (single-threaded arena)

**Files:** lib/Runtime/runtime.c
**Tests:** Arena alloc + reset cycle test.

### 1.7 Channel Destructor Ref-Counting (RFC-0009)

**Problem:** `chan_destroy` is a stub. Channels leak when dropped.

**Solution:** Channels already have ref_count field. Implement:
1. `__asc_chan_clone(chan)` — atomic increment ref_count
2. `__asc_chan_drop(chan)` — atomic decrement, if zero: drain remaining elements, free buffer, free channel struct
3. Wire DropInsertion to emit `__asc_chan_drop` for channel values

**Files:** lib/Runtime/channel_rt.c, lib/Analysis/DropInsertion.cpp
**Tests:** Channel dropped after all senders/receivers done — verify no leak.

### 1.8 Top-Level Panic Handler (RFC-0009)

**Problem:** Entry point doesn't catch panics. Unhandled panic goes to undefined behavior.

**Solution:** In CodeGen, wrap the user's main function:
1. Emit `__asc_panic_scope_begin()` before main body
2. Emit `__asc_panic_scope_end()` after main returns
3. If panic propagates to top level, call `__asc_top_level_panic_handler()` which prints PanicInfo and exits with code 101

**Files:** lib/CodeGen/PanicLowering.cpp, lib/Runtime/runtime.c
**Tests:** Program that panics — verify stderr output includes file/line and exit code 101.

### 1.9 NLL Error Provenance (RFC-0006)

**Problem:** Borrow errors report the conflict location but not the full chain (where the borrow was created, where it's used, why it conflicts).

**Solution:** Extend diagnostic emission in AliasCheck/MoveCheck:
1. For E001: show borrow creation location, conflicting access location, and the region span
2. For E003: show original borrow location, move location, and attempted use
3. Use MLIR Location attached to operations (already present) to provide source spans
4. Format as primary error + secondary notes (Rust-style)

**Files:** lib/Analysis/AliasCheck.cpp, lib/Analysis/MoveCheck.cpp
**Tests:** Verify error output includes note lines with borrow origin information.

### 1.10 Static Stack Size Analysis (RFC-0007)

**Problem:** Thread stack size is hardcoded. No analysis of actual stack requirements.

**Solution:** Conservative call-graph walk:
1. Build call graph from MLIR module (func.call → callee)
2. For each function, sum alloca sizes (available from LLVM lowering)
3. Walk call chains, accumulate max stack depth
4. Emit warning when spawned task's estimated stack exceeds configurable threshold (default 1MB)
5. Attach `stack_size` attribute to task.spawn operations for runtime to use

**Files:** New lib/Analysis/StackSizeAnalysis.cpp, lib/Driver/Driver.cpp
**Tests:** Test with deep recursion chain — verify warning emitted.

---

## Layer 2 — Compiler Completeness (RFCs 0001–0004, 0010, 0015)

**Prerequisite:** Layer 1 complete
**Target:** ~235 tests

### 2.1 Derive Macro System (RFC-0002, RFC-0011) — CRITICAL

**Problem:** No automatic derive for traits. Users must manually implement Clone, Copy, PartialEq, Hash, etc. for every type. Layer 3 std library depends on this heavily.

**Solution:** In Sema, when a struct/enum declaration has `@derive(TraitName)`:
1. `expandDerive()` generates a synthetic impl block
2. Per-trait expansion:
   - **Clone:** Field-by-field clone() calls
   - **Copy:** Marker trait — verify all fields are Copy, emit error if not
   - **PartialEq:** Field-by-field `==` with `&&` chain
   - **Eq:** Marker trait — verify PartialEq is derived/implemented
   - **Hash:** Field-by-field hash() calls with combine
   - **Ord/PartialOrd:** Lexicographic field comparison
   - **Debug:** Format as `TypeName { field1: value1, field2: value2 }`
   - **Default:** Field-by-field Default::default() calls
   - **Send/Sync:** Marker — verify all fields satisfy Send/Sync
   - **Serialize/Deserialize:** Generate to_json/from_json methods (for Layer 4 JSON)
3. Synthetic impls are injected into the AST before HIR lowering

**Files:** lib/Sema/SemaDecl.cpp (new expandDerive), include/asc/Sema/Sema.h
**Tests:** Derive each trait on a struct with mixed field types. Negative: derive(Copy) on struct with non-Copy field.

### 2.2 Constant Folding (RFC-0003)

**Problem:** arith.constant operations are not folded at compile time.

**Solution:** Register MLIR's built-in arith canonicalization patterns:
1. Add `arith::ArithDialect::getCanonicalizationPatterns()` to the pass pipeline
2. Run canonicalizer pass after HIR emission, before analysis passes
3. Handles: constant + constant → constant, multiply by 0/1, add 0, etc.

**Files:** lib/Driver/Driver.cpp
**Tests:** Verify `let x = 2 + 3` produces `arith.constant 5` (not add of two constants).

### 2.3 DWARF Debug Info (RFC-0010)

**Problem:** No source-level debugging for native targets.

**Solution:**
1. Create DIBuilder wrapper in HIRBuilder that manages compilation unit, file, and subprogram metadata
2. At each statement emission, attach MLIR FileLineCol location (already done for diagnostics)
3. During LLVM IR translation, locations become DILocation metadata
4. Pass `-g` flag through to LLVM TargetMachine for DWARF emission

**Files:** lib/HIR/HIRBuilder.cpp, lib/Driver/Driver.cpp
**Tests:** Compile with `--debug`, run `llvm-dwarfdump` on output, verify file/line entries.

### 2.4 Wasm Source Maps (RFC-0010)

**Problem:** No source mapping for Wasm output.

**Solution:**
1. After wasm-ld, read DWARF from the linked .wasm (if --debug)
2. Convert DWARF line tables to source map v3 JSON format
3. Write `.wasm.map` alongside output
4. Append `sourceMappingURL` custom section to .wasm

**Files:** lib/Driver/Driver.cpp (new emitSourceMap function)
**Tests:** Compile .wasm with --debug, verify .wasm.map exists and is valid JSON.

### 2.5 Slice Patterns (RFC-0015)

**Problem:** Can't destructure arrays with `[first, ...rest]` syntax.

**Solution:**
1. Parser: New PatternKind::Slice matching `[pattern, ...binding]`
2. Sema: Validate slice pattern against array/Vec types, infer rest as slice type
3. HIRBuilder: Emit bounds check, extract head element(s), compute rest pointer + length

**Files:** lib/Parse/ParseExpr.cpp, lib/Sema/SemaExpr.cpp, lib/HIR/HIRBuilder.cpp
**Tests:** Destructure array, match first element + rest. Nested slice pattern.

### 2.6 Labeled Break with Value (RFC-0015)

**Problem:** Labeled loops exist but break with value (`break 'label expr`) is incomplete.

**Solution:**
1. Parser: Allow expression after `break 'label`
2. HIRBuilder: Labeled loop block gets a result type matching the break expression
3. Lower to block argument passing on the exit branch

**Files:** lib/Parse/ParseStmt.cpp, lib/HIR/HIRBuilder.cpp
**Tests:** Loop that breaks with computed value. Nested loops with different break labels.

### 2.7 for-in Iteration Desugaring (RFC-0015)

**Problem:** `for (x of iter)` syntax doesn't desugar to Iterator::next() calls.

**Solution:** In HIRBuilder::visitForOfStmt():
1. Call `.into_iter()` on the collection expression (IntoIterator trait)
2. Emit loop: call `.next()` → match against Some(x) / None
3. Bind x in loop body, break on None

**Files:** lib/HIR/HIRBuilder.cpp
**Tests:** `for (x of vec)` iterating Vec elements. for-of over HashMap keys.

### 2.15 Operator Trait Dispatch (RFC-0015, RFC-0011)

**Problem:** Binary operators like `a + b` emit arith.addi directly. No dispatch to Add::add trait method for user-defined types.

**Solution:** In HIRBuilder, when emitting binary operations:
1. Check if operand types have a matching operator trait impl (Add, Sub, Mul, Div, etc.)
2. If so, emit a func.call to the trait method instead of an arith operation
3. Primitive types keep direct arith emission for performance
4. Index operator `a[i]` dispatches to Index::index trait method

**Files:** lib/HIR/HIRBuilder.cpp
**Tests:** Custom struct with Add impl — verify `a + b` calls the impl. Index operator on custom type.

### 2.16 Template Literal → format! Desugaring (RFC-0002)

**Problem:** Template literals (`` `hello ${name}` ``) parse but don't desugar to Display::fmt calls.

**Solution:** In HIRBuilder, when visiting template literal expressions:
1. Split template into string segments and expression holes
2. For each expression hole, call Display::fmt (or Debug::fmt for `${:?}`)
3. Concatenate results via String builder
4. This is the compiler-side wiring; the Display/Debug impls come from Layer 3

**Files:** lib/HIR/HIRBuilder.cpp
**Tests:** Template literal with i32 interpolation. Template with struct implementing Display.

### 2.17 Self-Hosting Validation Gate

**Problem:** Layer 3 assumes asc can compile its own std library (generics, traits, closures, iterators, pattern matching). If this isn't validated, Layer 3 hits walls.

**Solution:** Before declaring Layer 2 complete, compile a synthetic "canary" program that exercises:
1. Generic struct with trait bounds: `struct Wrapper<T: Display + Clone>`
2. Trait impl with method: `impl Display for Wrapper<T>`
3. Iterator chain: `.iter().map().filter().collect()`
4. Closure capture: `let f = |x| x + captured_var;`
5. Pattern matching: `match opt { Some(x) => x, None => default }`
6. for-of loop: `for (item of collection)`

If the canary compiles and runs, Layer 2 is complete. If not, fix before proceeding.

**Files:** test/e2e/self_hosting_canary.ts
**Tests:** Canary program compiles and produces expected output.

### 2.8 GPU Target Stubs (RFC-0004)

**Problem:** NVPTX64 and AMDGCN targets not accepted.

**Solution:**
1. Accept `--target nvptx64-nvidia-cuda` and `--target amdgcn-amd-amdhsa` in Driver
2. Create LLVM TargetMachine for these triples
3. Emit .ptx or .o output (no kernel launch — stub only as RFC specifies experimental)
4. Emit warning: "GPU target support is experimental"

**Files:** lib/Driver/Driver.cpp
**Tests:** Compile trivial function for each GPU target — verify output file produced.

### 2.9 Wasm Feature Gating (RFC-0004)

**Problem:** No way to enable/disable specific Wasm proposals.

**Solution:**
1. New `--wasm-features=+bulk-memory,+threads,-tail-call` flag
2. Parse features, pass to LLVM TargetMachine via subtarget features string
3. Default features: bulk-memory, mutable-globals, sign-ext (current behavior)

**Files:** lib/Driver/Driver.cpp
**Tests:** Compile with `--wasm-features=-bulk-memory`, verify feature not in .wasm.

### 2.10 asc fmt (RFC-0010)

**Problem:** Formatter is a stub.

**Solution:** Token-stream pretty printer:
1. Lex input file to token stream
2. Apply formatting rules: indentation (2 spaces), brace placement, spacing around operators, line length limit (100)
3. Write formatted output

**Files:** lib/Driver/Driver.cpp (expand runFmt)
**Tests:** Format a messy file, verify output matches expected. Roundtrip: format → reparse → AST equality.

### 2.11 asc doc (RFC-0010)

**Problem:** Doc generator is a stub.

**Solution:**
1. Parse input, collect doc comments (/// and /** */) associated with declarations
2. Emit one Markdown file per module: function signatures, type definitions, doc text
3. Cross-link types and functions

**Files:** lib/Driver/Driver.cpp (expand runDoc)
**Tests:** Generate docs for a file with doc comments, verify Markdown output.

### 2.12 LSP Completion (RFC-0010)

**Problem:** LSP has diagnostics, hover, completion but missing go-to-definition, references, rename, signature help.

**Solution:** Extend LSP server:
1. **textDocument/definition:** Resolve symbol at cursor → source Location from Sema symbol table
2. **textDocument/references:** Find all uses of symbol across parsed files
3. **textDocument/rename:** Validate rename (no keyword conflicts), find-and-replace all references
4. **textDocument/signatureHelp:** Show function parameter types at call sites

**Files:** lib/Driver/Driver.cpp (LSP section)
**Tests:** LSP request/response tests using JSON-RPC fixtures.

### 2.13 Fix-it Hints (RFC-0010)

**Problem:** No actionable suggestions in error messages.

**Solution:** Attach FixItHint to common diagnostics:
1. Missing semicolon → "insert ';' here"
2. Type mismatch with obvious coercion → "try `as i32`"
3. Unused variable → "prefix with '_' to suppress"
4. Missing return type → "add ': ReturnType'"

**Files:** lib/Sema/SemaExpr.cpp, lib/Sema/SemaDecl.cpp
**Tests:** Verify fix-it text appears in diagnostic output.

### 2.14 Nested Pattern Matching (RFC-0015)

**Problem:** Match exhaustiveness only checks flat patterns.

**Solution:** Recursive exhaustiveness checking in Sema:
1. For nested patterns (e.g., `Some(Ok(x))`), decompose into pattern tree
2. Check coverage at each level of nesting
3. Report missing patterns with full path (e.g., "missing: Some(Err(_))")

**Files:** lib/Sema/SemaExpr.cpp
**Tests:** Nested Option<Result<T,E>> match — verify exhaustiveness warning for missing arms.

---

## Layer 3 — Core Std Library (RFCs 0011–0014)

**Prerequisite:** Layer 2 complete (derive system, for-in desugaring, operator trait dispatch)
**Target:** ~270 tests
**Implementation language:** asc (TypeScript syntax), compiled by the asc compiler

### Build Order

All items below are self-hosted asc code in `std/`. Runtime C functions from `lib/Runtime/` provide the FFI layer (already exists for Vec, String, HashMap, Arc, Rc, sync primitives).

#### Phase 3.1: Core Traits (RFC-0011)

1. **std/core/ops.ts** — Operator trait impls for primitive types (i32 + i32 dispatches to Add::add)
2. **std/core/default.ts** — Default::default() for all primitives (0, 0.0, false, "")
3. **std/core/convert.ts** — From/Into/AsRef/AsMut impls between related types (i32→i64, String→&str)
4. **std/core/fmt.ts** — Display and Debug trait infrastructure:
   - Format string parser handling `{}` and `{:?}` placeholders
   - Formatter struct with write_str, write_fmt methods
   - Display/Debug impls for all primitive types
   - `format!` desugaring target (from template literals, per RFC-0002)
5. **std/core/iter.ts** — Iterator default methods:
   - map, filter, enumerate, zip, chain, take, skip, take_while, skip_while
   - flat_map, flatten, peekable, fuse, inspect
   - fold, reduce, collect, any, all, find, position, count
   - sum, product, min, max, min_by, max_by, nth
   - FromIterator impls for Vec, String, HashMap, HashSet

#### Phase 3.2: Memory Module (RFC-0012)

6. **std/mem/box.ts** — Box<T> with Deref/DerefMut impl, into_inner, leak
7. **std/mem/arc.ts** — Arc<T> full API: clone, try_unwrap, make_mut, ptr_eq, downgrade→Weak
8. **std/mem/rc.ts** — Rc<T> + Weak<T>: clone, try_unwrap, downgrade, upgrade, strong_count, weak_count
9. **std/mem/cell.ts** — Cell<T> (get/set for Copy types), RefCell<T> (borrow/borrow_mut with runtime tracking, panic on violation)
10. **std/mem/arena.ts** — Arena<T>: typed bump allocator, alloc returns &T tied to arena lifetime, reset frees all
11. **std/mem/manually_drop.ts** — ManuallyDrop<T> (suppresses Drop), MaybeUninit<T> (unsafe uninitialized)

#### Phase 3.3: Collections (RFC-0013)

12. **std/collections/vec.ts** — Vec<T> full API: all 17 existing methods + IntoIterator impl producing VecIter/VecIterRef/VecIterMut
13. **std/collections/hashmap.ts** — HashMap<K,V>: all 10 existing methods + Entry API (entry, or_insert, or_insert_with, and_modify, OccupiedEntry, VacantEntry) + IntoIterator
14. **std/collections/hashset.ts** — HashSet<T>: wrapper around HashMap<T,()> — insert, contains, remove, len, iter, intersection, union, difference, symmetric_difference
15. **std/collections/btreemap.ts** — BTreeMap<K,V>: B-tree with order 6 — insert, get, remove, range(start..end), keys, values, iter, len
16. **std/collections/static_array.ts** — StaticArray<T,N>: fixed-size stack array — index, len, iter, as_slice
17. **std/string/string.ts** — String full API: existing 16 methods + chars() iter, bytes() iter, lines(), split_whitespace(), repeat(n), replace, format integration

#### Phase 3.4: Concurrency & I/O (RFC-0014)

18. **std/thread/thread.ts** — Thread::spawn safe wrapper returning JoinHandle<T>, Thread::sleep, thread::current()
19. **std/sync/condvar.ts** — Condvar: wait(mutex), notify_one, notify_all
20. **std/sync/once.ts** — Once::call_once (atomic flag), Barrier::wait (counter + condvar)
21. **std/sync/channel.ts** — Safe channel API: channel<T>(n) for bounded, unbounded<T>() for unbounded. Sender<T>/Receiver<T> types with Send bound.
22. **std/io/read.ts + write.ts** — Read and Write traits with default methods (read_to_end, read_to_string, write_all, flush)
23. **std/io/stdio.ts** — stdin(), stdout(), stderr() global accessors wrapping WASI fd 0/1/2
24. **std/io/buf.ts** — BufReader<R: Read>, BufWriter<W: Write> with configurable buffer (default 8KB)
25. **std/io/file.ts** — File: open, create, read, write, seek, metadata, with Drop for auto-close
26. **std/net/tcp.ts** — TcpListener::bind, TcpListener::accept, TcpStream::connect, TcpStream read/write (WASI sockets)

---

## Layer 4 — Ecosystem Libraries (RFCs 0016–0020)

**Prerequisite:** Layer 3 complete (collections, iterators, I/O, formatting)
**Target:** ~310 tests (final)
**Implementation language:** asc (TypeScript syntax)

### Phase 4.1: Encoding (RFC-0018)

1. **std/encoding/base64.ts** — Full RFC 4648: encode, decode, encode_into, decode_into, encoded_len. Base64url variant. Padding handling.
2. **std/encoding/hex.ts** — Encode bytes→hex string, decode hex→bytes. Uppercase option. Odd-length handling.
3. **std/encoding/utf16.ts** — Encode String→u16[], decode u16[]→String. Surrogate pair handling. BOM detection.
4. **std/encoding/varint.ts** — Unsigned and signed LEB128 encode/decode. Read from byte buffer with advancement.

### Phase 4.2: Path & Env (RFC-0019)

5. **std/path/posix.ts** — basename, dirname, extname, stem, join, resolve, normalize, relative, is_absolute, is_relative, with_extname, with_basename. Handle edge cases: trailing slashes, `.`/`..`, empty.
6. **std/path/windows.ts** — Same API with drive letters, backslash normalization, UNC paths.
7. **std/env/env.ts** — get(key), set(key, val), remove(key), vars() iterator. WASI environ_get FFI.
8. **std/env/dotenv.ts** — Parse .env files: key=value, comments (#), quoted values, multiline with \, variable expansion ($VAR).

### Phase 4.3: Crypto (RFC-0018)

9. **std/crypto/sha256.ts** — SHA-256 per FIPS 180-4: init, update(bytes), finalize→[u8;32], digest(bytes) one-shot. Block processing with 64-byte chunks.
10. **std/crypto/sha512.ts** — SHA-512: same API, 128-byte blocks, 64-bit words.
11. **std/crypto/hmac.ts** — HMAC construction: HMAC-SHA256, HMAC-SHA512. ipad/opad per RFC 2104.
12. **std/crypto/password.ts** — Argon2id: memory-hard hashing with configurable memory, iterations, parallelism. bcrypt: Blowfish-based with salt generation.
13. **std/crypto/random.ts** — CSPRNG: fill(buf) via WASI random_get. random_u32(), random_u64(), random_range(min, max).

### Phase 4.4: JSON (RFC-0016)

14. **std/json/value.ts** — JsonValue enum: Null, Bool, Int(i64), Uint(u64), Float(f64), Str(String), Array(Vec<JsonValue>), Object(HashMap<String, JsonValue>). Accessor methods, type checks, indexing, deep clone/eq.
15. **std/json/parser.ts** — RFC 8259 recursive descent: all string escapes (\uXXXX, \n, \t, \\, etc.), number edge cases (leading zeros, exponents), nested depth limit (configurable, default 128), clear error messages with position.
16. **std/json/serializer.ts** — Recursive serializer: compact and pretty-print (configurable indent). String escaping. Handles cycles via depth limit.
17. **std/json/slice.ts** — JsonSlice zero-copy: scan tokens without allocating, return &str slices into source buffer. Lazy value resolution.
18. **std/json/serde.ts** — Serialize/Deserialize traits with visitor pattern. Manual impls for all primitive and std types. derive(Serialize, Deserialize) generates field-by-field traversal.

### Phase 4.5: Config (RFC-0019)

19. **std/config/toml.ts** — TOML v1.0 parser: bare/quoted keys, basic/literal strings, multiline, integers (hex/oct/bin), floats (inf/nan), booleans, datetimes, arrays, inline tables, tables, array of tables.
20. **std/config/yaml.ts** — YAML 1.2 subset: block mappings, block sequences, flow mappings, flow sequences, scalars (plain/quoted), anchors/aliases, merge keys. No tags.
21. **std/config/ini.ts** — INI parser: sections [name], key=value, key:value, comments (;#), multiline with \, duplicate key handling (last wins).

### Phase 4.6: Extended Collections (RFC-0017)

22. **std/collections/btreeset.ts** — BTreeSet<T>: wrapper around BTreeMap<T,()>. insert, contains, remove, range, iter, intersection, union, difference.
23. **std/collections/linked_list.ts** — LinkedList<T>: doubly-linked with sentinel node. push_front/back, pop_front/back, insert_before/after, cursor API, iter.
24. **std/collections/vecdeque.ts** — VecDeque<T>: ring buffer. push_front/back, pop_front/back, get, rotate_left/right, iter, make_contiguous.
25. **std/collections/heap.ts** — BinaryHeap<T>: max-heap. push, pop, peek, into_sorted_iter, heapify. Configurable comparator for min-heap.
26. **std/collections/priority.ts** — PriorityQueue<K,V>: BinaryHeap keyed by priority. push(priority, value), pop, peek, change_priority.

### Phase 4.7: Async Utilities (RFC-0020)

27. **std/async/semaphore.ts** — Counting semaphore: acquire (blocking), try_acquire (non-blocking), acquire_timeout, release. Built on Mutex + Condvar.
28. **std/async/retry.ts** — retry<T>(config, fn): RetryConfig with max_attempts, initial_delay, max_delay, multiplier, jitter, retry_if predicate. Returns Result<T, RetryError>.
29. **std/async/debounce.ts** — debounce<T>(delay, fn): returns debounced function. Timer-based — resets on each call, fires after quiet period.
30. **std/async/deadline.ts** — deadline<T>(timeout, fn): runs fn, returns Result<T, TimeoutError>. Spawns watchdog thread, cancels on completion.
31. **std/async/pool.ts** — TaskPool: fixed workers, submit(fn)→JoinHandle, map(collection, fn)→Vec, shutdown. Work queue via MPMC channel.
32. **std/async/mux.ts** — MuxAsyncIterator<T>: add(Receiver<T>), next()→Option<T>. Round-robin poll across receivers.

---

## Validation Strategy

### Per-Layer Gates

Each layer must pass before the next begins:

| Gate | Criteria |
|------|----------|
| Layer 1 exit | All RFC 0005–0009 features implemented. All existing + new tests pass. No known double-free, use-after-free, or data race in test suite. |
| Layer 2 exit | All RFC 0001–0004, 0010, 0015 features implemented. derive system works for all traits. asc fmt/doc/lsp functional. |
| Layer 3 exit | All std library types compile with asc compiler. Iterator chains work. Collection + concurrency integration tests pass. |
| Layer 4 exit | All ecosystem libraries compile and pass tests. JSON roundtrip, crypto known-answer tests, config parse tests all pass. |

### Test Targets

| Layer | New Tests | Cumulative |
|-------|-----------|------------|
| Layer 1 | ~16 | ~215 |
| Layer 2 | ~20 | ~235 |
| Layer 3 | ~35 | ~270 |
| Layer 4 | ~40 | ~310 |

### Regression Policy

Every build runs full `lit test/` suite. Zero regressions allowed between layers.

---

## RFC Coverage Projection

| RFC | Current | After L1 | After L2 | After L3 | After L4 |
|-----|---------|----------|----------|----------|----------|
| 0001 | 100% | 100% | 100% | 100% | 100% |
| 0002 | 90% | 90% | 100% | 100% | 100% |
| 0003 | 95% | 95% | 100% | 100% | 100% |
| 0004 | 83% | 83% | 100% | 100% | 100% |
| 0005 | 85% | 100% | 100% | 100% | 100% |
| 0006 | 90% | 100% | 100% | 100% | 100% |
| 0007 | 40% | 100% | 100% | 100% | 100% |
| 0008 | 65% | 100% | 100% | 100% | 100% |
| 0009 | 60% | 100% | 100% | 100% | 100% |
| 0010 | 95% | 95% | 100% | 100% | 100% |
| 0011 | 93% | 93% | 93% | 100% | 100% |
| 0012 | 60% | 60% | 60% | 100% | 100% |
| 0013 | 82% | 82% | 82% | 100% | 100% |
| 0014 | 72% | 72% | 72% | 100% | 100% |
| 0015 | 90% | 90% | 100% | 100% | 100% |
| 0016 | 25% | 25% | 25% | 25% | 100% |
| 0017 | 5% | 5% | 5% | 5% | 100% |
| 0018 | 30% | 30% | 30% | 30% | 100% |
| 0019 | 20% | 20% | 20% | 20% | 100% |
| 0020 | 15% | 15% | 15% | 15% | 100% |
| **Weighted** | **78.5%** | **~88%** | **~93%** | **~97%** | **100%** |
