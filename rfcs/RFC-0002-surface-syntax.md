# RFC-0002 — Surface Syntax

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0001 |
| Key principle | Infer ownership wherever possible; annotate only at boundaries |

## Summary

This RFC defines the surface syntax of the AssemblyScript language as seen by users. The
syntax is TypeScript-compatible for all intra-function code. Ownership annotations appear
only at function signatures and concurrency boundaries, and only when inference cannot
resolve ambiguity. The compiler never requires annotations inside function bodies.

## Ownership Type Annotations

Three ownership types are available at the type level:

| Annotation | Meaning |
|---|---|
| `own<T>` | Sole owner of a T. Required at signatures when a value crosses a thread or module boundary. |
| `ref<T>` | Shared borrow — read-only reference. Inferred inside function bodies; optional at signatures. |
| `refmut<T>` | Exclusive mutable borrow. Inferred inside bodies; required at signatures when caller retains ownership but grants mutation. |

These annotations are erased after Sema — they exist only in the AST and HIR. LLVM IR
contains no ownership metadata.

## Inference Rules — Inside Function Bodies

The compiler infers ownership for all local variable declarations. No annotation is required
when the following rules are unambiguous:

1. A variable whose value is never used after a function call → assumed **moved** into that call.
2. A variable passed to a function that does not consume it → assumed **borrowed**.
3. A variable returned from a function → **transfers ownership** to the caller.
4. A variable used in two or more branches where it is moved in one branch → triggers a
   **drop flag** (see RFC-0008). The compiler emits a warning for conditional moves; it is
   not an error.

The borrow checker (RFC-0006) verifies all inferred ownership assignments after HIR
construction. If inference produces a provably incorrect assignment, Sema emits an error
before HIR construction begins.

## Annotation Requirement Rules — Function Boundaries

Annotations are **required** at function signatures when:

1. A parameter is moved into a spawned task (must be `own<T>` and `T` must implement `Send`).
2. A function returns a heap-allocated value across a module boundary.
3. A function takes mutable access to a value owned by the caller (`refmut<T>`).
4. A generic function's ownership behaviour depends on a type parameter (annotate the
   constraint: `<T: Send>`).

Annotations are **optional but permitted** at function signatures to make ownership explicit
for documentation or to assist inference in complex cases.

## New Keywords and Constructs

The following additions are made to the AssemblyScript grammar:

| Syntax | Meaning |
|---|---|
| `own<T>` | Ownership type annotation |
| `ref<T>` | Shared borrow annotation (optional inside bodies) |
| `refmut<T>` | Exclusive mutable borrow annotation |
| `task.spawn(() => { ... })` | Spawn a concurrent task; captures must be `own<T>` where `T: Send` |
| `chan<T>(n)` | Create a bounded channel of capacity `n`; returns `[tx, rx]` |
| `tx.send(v)` | Send a value — consumes `v` (moves ownership into channel) |
| `rx.recv()` | Receive a value — produces a new owned value |
| `@heap` | Declaration attribute: force heap allocation for this binding |
| `@copy` | Type attribute: mark a type as `Copy` (bitwise-copyable, no drop) |
| `@send` | Type attribute: mark a type as `Send` (safe to move across thread boundary) |
| `@sync` | Type attribute: mark a type as `Sync` (safe to share references across threads) |

## Syntax Examples

### Fully inferred — no annotations required

```typescript
function transform(input: Buffer): Output {
  const result = new Output();     // inferred: own.alloc<Output>
  result.write(input.slice(0, 8)); // inferred: borrow.ref<Buffer> for slice call
  return result;                   // inferred: own.move<Output> to caller
}                                  // input: own.drop<Buffer> inserted at scope exit
```

### Hybrid — annotation only at concurrency boundary

```typescript
async function pipeline(data: own<Buffer>): Promise<Result> {
  const [tx, rx] = chan<Result>(1);
  task.spawn(() => {
    tx.send(transform(data)); // data moved into task — compiler verifies Send
  });
  return await rx.recv();     // blocks until result arrives
}
```

### Explicit mutable borrow at signature

```typescript
function fillBuffer(buf: refmut<Buffer>, value: u8): void {
  buf.fill(value); // mutation allowed; caller retains ownership after return
}
```

### Conditional move — drop flag emitted, warning issued

```typescript
function either(flag: bool, a: own<Buf>, b: own<Buf>): own<Buf> {
  return flag ? a : b;
  // unused branch: drop flag checked at join point
  // compiler warning: conditional move of `a` or `b`
}
```

### Copy type — no ownership tracking needed

```typescript
@copy
struct Point { x: f64; y: f64; }

function midpoint(a: Point, b: Point): Point {
  // Point is Copy — no moves, no drops, no annotations
  return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
}
```

### Custom Send type

```typescript
@send
struct WorkItem { id: u32; payload: ArrayBuffer; }

function process(items: own<WorkItem[]>): void {
  const [tx, rx] = chan<Result>(items.length);
  for (const item of items) {
    task.spawn(() => { tx.send(doWork(item)); }); // WorkItem is Send — ok
  }
}
```

## Grammar Changes (EBNF delta)

```ebnf
TypeAnnotation  ::= ... | OwnType | RefType | RefMutType
OwnType         ::= "own" "<" Type ">"
RefType         ::= "ref" "<" Type ">"
RefMutType      ::= "refmut" "<" Type ">"

TaskSpawn       ::= "task" "." "spawn" "(" ArrowFunction ")"
ChanCreate      ::= "chan" "<" Type ">" "(" Expression ")"

Declaration     ::= Attribute* Declaration
Attribute       ::= "@" Identifier
```

All other grammar rules remain unchanged from AssemblyScript's existing grammar.

## Interaction with Existing AssemblyScript Features

| Feature | Behaviour |
|---|---|
| `class` | Class instances are `own<T>` by default. Fields may be `ref<T>` with explicit annotation. |
| `ArrayBuffer` | Treated as `own<ArrayBuffer>` — moved or dropped, never GC'd. |
| `StaticArray<T>` | `own<StaticArray<T>>` — ownership of the array and its elements. |
| `string` | Immutable; treated as `own<string>` with Copy semantics when `T: Copy`. |
| `null` | Owned nullable: `own<T> \| null`. Borrow checker tracks null vs non-null separately. |
| Closures | Captures are explicit `own<T>` moves unless annotated `ref<T>`. |
