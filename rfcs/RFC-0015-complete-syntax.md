# RFC-0015 — Complete Supported Syntax

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0002 |
| Supersedes | RFC-0002 (syntax sections only — RFC-0002 remains authoritative for ownership semantics) |

## Summary

This RFC is the single authoritative reference for every syntactic construct accepted by
the `asc` compiler. It defines the complete supported TypeScript subset, every ownership
extension, every construct that is explicitly excluded, and the full EBNF grammar. If a
construct is not listed here as supported, the compiler rejects it.

---

## 1. Primitive Types

All primitive types are `@copy` (bitwise-copyable, no destructor).

| Type | Width | Notes |
|------|-------|-------|
| `i8` | 8-bit signed | |
| `i16` | 16-bit signed | |
| `i32` | 32-bit signed | default integer type |
| `i64` | 64-bit signed | |
| `i128` | 128-bit signed | software-emulated |
| `u8` | 8-bit unsigned | |
| `u16` | 16-bit unsigned | |
| `u32` | 32-bit unsigned | |
| `u64` | 64-bit unsigned | |
| `u128` | 128-bit unsigned | software-emulated |
| `f32` | 32-bit float | IEEE 754 |
| `f64` | 64-bit float | IEEE 754 |
| `bool` | 1 logical bit | `true` / `false` literals |
| `char` | 32-bit Unicode | Unicode scalar value |
| `usize` | pointer-width unsigned | 32-bit on wasm32 |
| `isize` | pointer-width signed | 32-bit on wasm32 |
| `void` | zero-size | function return only |
| `never` | bottom type | diverging functions, `panic!` |

---

## 2. Literals

### Numeric literals

```typescript
42          // i32 (default integer)
42i8        // i8 suffix
42u32       // u32 suffix
42i64       // i64 suffix
42usize     // usize suffix
3.14        // f64 (default float)
3.14f32     // f32 suffix
0xFF        // hex integer (u32 by default)
0o77        // octal integer
0b1010      // binary integer
1_000_000   // underscores allowed in any numeric literal
```

### String and character literals

```typescript
"hello"             // string literal — ref<str> with static lifetime
'a'                 // char literal — Unicode scalar value U+0061
'\n'  '\t'  '\\'  '\''  '\"'  '\0'   // escape sequences
'\u{1F600}'         // Unicode escape in char
"\u{1F600}"         // Unicode escape in string
r"raw \n string"    // raw string — no escape processing
r#"raw with "quotes""#  // raw string with delimiter
```

### Template literals (string interpolation)

```typescript
`Hello, ${name}!`           // desugars to: format!("Hello, {}!", name)
`${a} + ${b} = ${a + b}`    // each interpolation calls Display::fmt
```

### Other literals

```typescript
true   false          // bool
null                  // Option::None in nullable context (own<T> | null)
[1, 2, 3]            // array literal — StaticArray<i32, 3>
[0; 32]              // array repeat — StaticArray<i32, 32> filled with 0
{ x: 1, y: 2 }      // struct literal (when type is known from context)
```

---

## 3. Variables and Bindings

```typescript
// Immutable binding (default)
const x = 42;
const x: i32 = 42;

// Mutable binding
let x = 42;
let x: i32 = 42;

// Destructuring
const [a, b] = [1, 2];
const { x, y } = point;
const { x: px, y: py } = point;   // rename fields

// Ownership annotations (optional — inferred when unambiguous)
const buf: own<Buffer> = Buffer::new(1024);
```

`const` bindings are immutable — you cannot rebind or mutate through them.
`let` bindings are mutable — you can rebind and mutate through them.
Neither is `const`-evaluated at compile time (use `static` for that).

---

## 4. Operators

### Arithmetic

```typescript
a + b    a - b    a * b    a / b    a % b    // binary
-a                                            // unary negation
a += b   a -= b   a *= b   a /= b   a %= b  // compound assignment
```

Integer overflow: wrapping in release, panic in debug (configurable via `--overflow`).

### Bitwise

```typescript
a & b    a | b    a ^ b    ~a       // AND, OR, XOR, NOT
a << b   a >> b                     // shift (logical for unsigned, arithmetic for signed)
a &= b   a |= b   a ^= b
a <<= b  a >>= b
```

### Comparison

```typescript
a == b   a != b   a < b   a > b   a <= b   a >= b
```

All comparison operators return `bool`. Chained comparisons (`a < b < c`) are **not**
supported — write `a < b && b < c`.

### Logical

```typescript
a && b   a || b   !a
```

Short-circuit: `&&` and `||` do not evaluate the right operand if the result is determined
by the left.

### String

```typescript
a + b    // String concatenation: consumes own<String>, borrows ref<str>
```

### Range

```typescript
a..b     // exclusive range [a, b)  — RangeTo<T>
a..=b    // inclusive range [a, b]  — RangeInclusive<T>
..b      // range from start to b   — RangeTo<T>
a..      // range from a to end     — RangeFrom<T>
..       // full range              — RangeFull
```

### Casting

```typescript
a as i64    // numeric cast — never panics, may truncate
```

Only numeric primitive casts are supported. Pointer casts require `unsafe`.

### `?` Operator

```typescript
expr?   // propagate Result::Err or Option::None — see RFC-0013
```

### Index

```typescript
arr[i]      // Index::index — returns ref<T>
arr[i] = v  // IndexMut::index_mut — returns refmut<T>
```

---

## 5. Control Flow

### `if` / `else`

```typescript
if condition { ... }
if condition { ... } else { ... }
if condition { ... } else if condition { ... } else { ... }

// if as expression (all branches must have same type)
const x = if flag { 1 } else { 2 };
```

### `while`

```typescript
while condition { ... }

// while let — pattern-matching loop
while let Some(v) = iter.next() { ... }
```

### `loop`

```typescript
loop { ... }                   // infinite loop
loop { if done { break; } }   // break exits

// loop as expression — break with value
const result = loop {
  if done { break value; }
};
```

### `for...of`

```typescript
for (const x of collection) { ... }   // desugars to IntoIterator::into_iter()
for (const x of vec.iter()) { ... }   // borrowing iterator
for (const x of vec.iter_mut()) { ... } // mutable borrow iterator
```

`for...of` does **not** support plain numeric ranges from TypeScript (e.g.
`for (let i = 0; i < n; i++)`). Use range syntax instead:

```typescript
for (const i of 0..n) { ... }    // i: usize, range [0, n)
```

### `break` / `continue`

```typescript
break;              // exit loop
break value;        // exit loop with value (loop expression only)
continue;           // next iteration

// Labelled break/continue
outer: for (const x of xs) {
  for (const y of ys) {
    if done { break outer; }
    if skip { continue outer; }
  }
}
```

### `return`

```typescript
return;          // void return
return value;    // return expression
```

### `match`

```typescript
match value {
  Pattern => expression,
  Pattern => { block },
  _ => default,
}

// match as expression
const name = match code {
  200 => "ok",
  404 => "not found",
  _ => "unknown",
};
```

### `throw` / `panic`

```typescript
panic!("message");           // unconditional panic
panic!("value: {}", x);     // formatted panic message
```

`throw` is not supported. Use `Result<T,E>` and `?` for error propagation.

---

## 6. Pattern Matching

Patterns appear in `match`, `if let`, `while let`, destructuring bindings, and function
parameters.

```typescript
// Literal patterns
match x {
  0 => "zero",
  1 | 2 => "one or two",
  3..=9 => "three to nine",
  _ => "other",
}

// Binding patterns
match opt {
  Some(v) => use(v),    // v: own<T>
  None => {},
}

// Struct patterns
match point {
  { x: 0, y } => ...,
  { x, y: 0 } => ...,
  { x, y } => ...,
}

// Tuple patterns
match pair {
  (0, _) => ...,
  (a, b) => ...,
}

// Array patterns
match arr {
  [first, ..rest] => ...,
  [a, b, c] => ...,
}

// Guard patterns
match x {
  n if n < 0 => "negative",
  n if n > 0 => "positive",
  _ => "zero",
}

// if let — single-arm match
if let Some(v) = opt { use(v); }
if let Ok(v) = result { use(v); } else { handle_err(); }

// let-else — diverge on mismatch
let Some(v) = opt else { return; };
let Ok(v) = result else { panic!("expected ok"); };
```

---

## 7. Functions

```typescript
// Basic function
function add(a: i32, b: i32): i32 {
  return a + b;
}

// Expression body (implicit return)
function add(a: i32, b: i32): i32 { a + b }

// Void function
function log(msg: ref<str>): void {
  println!("{}", msg);
}

// Ownership-annotated parameters
function process(data: own<Buffer>): own<Result> { ... }
function inspect(data: ref<Buffer>): usize { data.len() }
function fill(data: refmut<Buffer>, value: u8): void { data.fill(value); }

// Generic functions
function identity<T>(x: own<T>): own<T> { x }
function max<T: Ord>(a: own<T>, b: own<T>): own<T> { if a >= b { a } else { b } }

// Multiple generic bounds
function serialize<T: Display + Debug>(v: ref<T>): own<String> { format!("{}", v) }

// Where clause (for complex bounds)
function transform<T, U>(v: own<T>): own<U>
  where T: Into<U>, U: Default
{ v.into() }

// Default parameters — NOT supported (use overloading or builder pattern)
// Named parameters — NOT supported (use struct arguments)

// Async functions — NOT supported in this RFC (future RFC)
```

### Arrow functions / closures

```typescript
// Closure expression
const double = (x: i32) => x * 2;
const add = (a: i32, b: i32) => a + b;
const greet = (name: ref<str>) => {
  println!("Hello, {}!", name);
};

// Closure with explicit capture modes
// Captures are inferred by the borrow checker:
// - If a captured variable is moved into the closure → own<T>
// - If only borrowed → ref<T> or refmut<T>

// Closure types:
// FnOnce — can be called once (captures by own<T>)
// Fn     — can be called multiple times (captures by ref<T>)
// FnMut  — can be called multiple times, mutates captures (captures by refmut<T>)

// Higher-order functions
function apply<F: FnOnce(i32) -> i32>(f: own<F>, x: i32): i32 { f(x) }
```

---

## 8. Structs

```typescript
// Named struct
struct Point {
  x: f64,
  y: f64,
}

// Struct with ownership-annotated fields
struct Request {
  method: own<String>,
  path: own<String>,
  body: Option<own<Vec<u8>>>,
}

// Unit struct (zero-size)
struct Marker;

// Struct literal
const p = Point { x: 1.0, y: 2.0 };
const p: Point = { x: 1.0, y: 2.0 };   // type-inferred form

// Struct update syntax
const p2 = Point { x: 3.0, ..p };   // p.y is moved into p2

// Field access
const x = p.x;        // copy if f64 is Copy
const s = req.method; // moves method out of req

// Methods
impl Point {
  fn new(x: f64, y: f64): own<Point> {
    Point { x, y }
  }

  fn distance(ref<Point>, other: ref<Point>): f64 {
    ((self.x - other.x).powi(2) + (self.y - other.y).powi(2)).sqrt()
  }

  fn translate(refmut<Point>, dx: f64, dy: f64): void {
    self.x += dx;
    self.y += dy;
  }
}
```

### `@copy` structs

```typescript
@copy
struct Rect { x: f64, y: f64, w: f64, h: f64 }
// All fields must be @copy. No Drop impl allowed.
```

### `@send` / `@sync` structs

```typescript
@send @sync
struct WorkItem { id: u32, payload: own<Vec<u8>> }
// Asserts the type is Send and Sync — Sema verifies field-by-field
```

---

## 9. Enums

```typescript
// C-like enum (all variants @copy, no payload)
enum Direction { North, South, East, West }
enum Status { Ok = 200, NotFound = 404, Error = 500 }  // explicit discriminants

// Algebraic enum (data-carrying variants)
enum Shape {
  Circle { radius: f64 },
  Rect { width: f64, height: f64 },
  Triangle { base: f64, height: f64 },
}

// Option-style enum
enum Option<T> {
  Some(own<T>),
  None,
}

// Result-style enum
enum Result<T, E> {
  Ok(own<T>),
  Err(own<E>),
}

// Using enums
const d = Direction::North;
const s = Shape::Circle { radius: 1.0 };

match s {
  Shape::Circle { radius } => println!("circle r={}", radius),
  Shape::Rect { width, height } => println!("rect {}x{}", width, height),
  Shape::Triangle { base, height } => println!("triangle"),
}
```

---

## 10. Traits

```typescript
// Trait definition
trait Animal {
  fn name(ref<Self>): ref<str>;
  fn sound(ref<Self>): own<String>;
  fn legs(ref<Self>): u32 { 4 }  // default implementation
}

// Trait implementation
struct Dog { name: own<String> }

impl Animal for Dog {
  fn name(ref<Dog>): ref<str> { self.name.as_str() }
  fn sound(ref<Dog>): own<String> { "woof".to_string() }
}

// Trait bounds
function describe<T: Animal>(animal: ref<T>): void {
  println!("{} says {}", animal.name(), animal.sound());
}

// Trait objects (dynamic dispatch)
function describe_any(animal: ref<dyn Animal>): void {
  println!("{}", animal.name());
}

// Box<dyn Trait> for owned trait objects
function make_animal(kind: ref<str>): own<Box<dyn Animal>> { ... }

// Supertrait
trait Pet: Animal {
  fn owner(ref<Self>): ref<str>;
}

// Associated types
trait Container {
  type Item;
  fn get(ref<Self>, i: usize): Option<ref<Item>>;
}

// Associated constants
trait HasDefault {
  const DEFAULT: Self;
}
```

---

## 11. Generics

```typescript
// Generic struct
struct Stack<T> {
  items: own<Vec<T>>,
}

// Generic enum (see Section 9)

// Generic impl
impl<T> Stack<T> {
  fn push(refmut<Stack<T>>, v: own<T>): void { self.items.push(v); }
  fn pop(refmut<Stack<T>>): Option<own<T>> { self.items.pop() }
}

// Const generics
struct Matrix<T, const N: usize, const M: usize> {
  data: own<StaticArray<T, {N * M}>>,
}

// Generic constraints
fn largest<T: Ord>(list: ref<[T]>): ref<T> { ... }

// Lifetime parameters — expressed as region tokens at the HIR level,
// not as explicit syntax in the surface language.
// The compiler infers all region/lifetime information automatically.
// Explicit lifetime annotations are NOT part of the surface syntax.
```

---

## 12. Concurrency Syntax

```typescript
// Spawn a task (RFC-0007)
const handle = task.spawn(() => {
  // task body — captured values are moved in
  compute()
});

// Join
const result = handle.join();   // blocks; returns Result<R, PanicInfo>

// Channel
const [tx, rx] = chan<i32>(16);    // bounded channel, capacity 16
const [tx, rx] = chan<i32>();      // unbounded channel

tx.send(value);                    // moves value; returns Result<void, SendError>
const v = rx.recv();               // blocks; returns Result<own<T>, RecvError>

// Select
select! {
  v = rx1.recv() => { handle(v); },
  v = rx2.recv() => { handle(v); },
}

// Mutex
const m = Mutex::new(initial_value);
{
  const guard = m.lock().unwrap();  // guard: MutexGuard<T>
  guard.mutate();
  // guard dropped here → lock released
}
```

---

## 13. Modules and Imports

```typescript
// Import from std
import { Vec, HashMap } from 'std/collections';
import { println } from 'std/fmt';
import { Rc, Weak } from 'std/rc';   // explicit import required for Rc

// Import from local module
import { Parser } from './parser';
import { type Token } from './lexer';  // type-only import

// Re-export
export { Parser };
export { Parser as AscParser };

// Wildcard import — NOT supported (ambiguity with ownership inference)

// Module declaration
// Each .ts file is one module.
// No explicit module {} blocks — file boundary = module boundary.

// Export
export function parse(input: ref<str>): own<Ast> { ... }
export class Lexer { ... }
export enum TokenKind { ... }
export type Result<T> = std::Result<T, own<AscError>>;  // type alias
```

---

## 14. Attributes

Attributes annotate declarations and affect compiler behaviour.

```typescript
// Type attributes (on struct/enum)
@copy        // Mark as Copy — Sema verifies all fields are Copy, no Drop impl
@send        // Assert Send — Sema verifies field-by-field
@sync        // Assert Sync — Sema verifies field-by-field
@repr(C)     // C layout — fields in declaration order, no reordering
@repr(packed)// Packed layout — no padding between fields

// Declaration attributes (on functions/variables)
@heap        // Force heap allocation for this binding
@inline      // Hint to always inline
@cold        // Hint that this function is rarely called (e.g. panic handlers)
@test        // Mark as a unit test function — compiled only in test mode
@bench       // Mark as a benchmark function
@deprecated("message")  // Emit deprecation warning on use
@allow(lint_name)        // Suppress a specific lint
@deny(lint_name)         // Promote a lint to an error
@warn(lint_name)         // Ensure a lint emits a warning

// Unsafe attribute
@unsafe      // Mark function as requiring unsafe call site
```

---

## 15. `unsafe` Blocks

```typescript
// Unsafe block — permits raw pointer ops, FFI calls, @unsafe functions
unsafe {
  const val = ptr::read(raw_ptr);
  ptr::write(raw_ptr, new_val);
}

// Unsafe function
@unsafe
function dangerous(ptr: *const u8, len: usize): ref<[u8]> {
  ptr::slice_from_raw_parts(ptr, len)
}

// Calling an unsafe function
const slice = unsafe { dangerous(ptr, len) };
```

---

## 16. FFI (Foreign Function Interface)

```typescript
// Declare external function (Wasm import)
@extern("env", "log_string")
function extern_log(ptr: *const u8, len: usize): void;

// Export function to host (Wasm export)
@export("my_function")
function my_function(x: i32): i32 { x * 2 }

// Calling extern
unsafe { extern_log(s.as_ptr(), s.len()); }
```

---

## 17. Macros (Built-in Only)

The compiler provides a fixed set of built-in macros. User-defined macros are **not**
supported in this version.

| Macro | Description |
|-------|-------------|
| `format!(fmt, args...)` | Allocate formatted `own<String>` |
| `write!(sink, fmt, args...)` | Write to any `Write` implementor |
| `writeln!(sink, fmt, args...)` | Write with trailing newline |
| `print!(fmt, args...)` | Write to stdout |
| `println!(fmt, args...)` | Write to stdout with newline |
| `eprint!(fmt, args...)` | Write to stderr |
| `eprintln!(fmt, args...)` | Write to stderr with newline |
| `panic!(msg)` / `panic!(fmt, args...)` | Trigger a panic |
| `assert!(cond)` / `assert!(cond, msg)` | Panic if condition is false |
| `assert_eq!(a, b)` / `assert_ne!(a, b)` | Panic if equal/not-equal |
| `debug_assert!(...)` | Assert only in debug builds |
| `todo!()` | Placeholder — panics with "not yet implemented" |
| `unimplemented!()` | Panics with "not implemented" |
| `unreachable!()` | Panics with "entered unreachable code" |
| `dbg!(expr)` | Print expression and value to stderr, return value |
| `size_of!(<T>)` | Compile-time size of type |
| `align_of!(<T>)` | Compile-time alignment of type |
| `select!` | Multi-channel wait (RFC-0014) |

---

## 18. Type Aliases

```typescript
type Result<T> = std::Result<T, own<AscError>>;
type Callback = own<Box<dyn FnOnce(i32) -> i32>>;
type Matrix4 = StaticArray<f32, 16>;
```

---

## 19. Constants and Statics

```typescript
// Compile-time constant — value must be a const expression
const MAX_SIZE: usize = 1024;
const PI: f64 = 3.141592653589793;
const ZERO_RECT: Rect = Rect { x: 0.0, y: 0.0, w: 0.0, h: 0.0 };  // Rect must be @copy

// Static — exists for the lifetime of the program
static COUNTER: AtomicI32 = AtomicI32::new(0);
static mut BUFFER: [u8; 1024] = [0; 1024];   // mutable static — unsafe to access
```

Const expressions support: literals, arithmetic, comparison, `size_of!`, `align_of!`,
const fn calls, and `@copy` struct construction. No heap allocation in const expressions.

---

## 20. Comments and Documentation

```typescript
// Line comment

/* Block comment */

/// Documentation comment — attached to the next item
/// Supports Markdown in doc comments.
/// # Example
/// ```
/// const x = add(1, 2);
/// assert_eq!(x, 3);
/// ```
function add(a: i32, b: i32): i32 { a + b }

/** JSDoc-style block doc comment — also supported */
function subtract(a: i32, b: i32): i32 { a - b }
```

---

## 21. Explicitly Unsupported TypeScript Features

The following TypeScript features are **not supported** and produce a Sema error:

| Feature | Reason |
|---------|--------|
| `any` type | Incompatible with ownership inference |
| `unknown` type | Same |
| Union types beyond `T \| null` | GC-free ownership requires statically known layout |
| Intersection types | Same |
| Structural typing / duck typing | Nominal type system only |
| `typeof` expressions | Runtime type info not available |
| `instanceof` | Same |
| Decorators (`@decorator` on classes) | Use attribute syntax instead |
| `class` with inheritance | Use traits for polymorphism |
| `extends` on classes | Same |
| `implements` (class keyword) | Use `impl Trait for Type` |
| `interface` | Use `trait` instead |
| `namespace` | Use module files instead |
| `enum` with computed members | Discriminants must be literals |
| `readonly` modifier | Use `ref<T>` borrow instead |
| Optional chaining `?.` | Use `Option::map` or `if let Some` |
| Nullish coalescing `??` | Use `Option::unwrap_or` |
| Non-null assertion `!` | Use `Option::unwrap()` explicitly |
| `async` / `await` | Future RFC |
| Generators / `yield` | Not planned |
| Proxies | No runtime reflection |
| Symbol type | Not supported |
| `WeakRef` / `FinalizationRegistry` | GC-specific |
| `eval` | No JIT |
| Dynamic `import()` | Static imports only |
| `try` / `catch` / `finally` | Use `Result<T,E>` and `?` |
| `for...in` (over object keys) | No reflection |

---

## 22. Complete EBNF Grammar (Abbreviated)

```ebnf
Program       ::= Item*
Item          ::= FunctionDef | StructDef | EnumDef | TraitDef | ImplBlock
                | TypeAlias | ConstDef | StaticDef | ImportDecl | ExportDecl

FunctionDef   ::= Attribute* "function" Identifier GenericParams?
                  "(" ParamList ")" ":" Type Block
ParamList     ::= (Param ("," Param)*)? ","?
Param         ::= Identifier ":" Type

StructDef     ::= Attribute* "struct" Identifier GenericParams?
                  ("{" FieldList "}" | ";")
FieldList     ::= (Field ("," Field)*)? ","?
Field         ::= Identifier ":" Type

EnumDef       ::= Attribute* "enum" Identifier GenericParams?
                  "{" Variant ("," Variant)* ","? "}"
Variant       ::= Identifier ("{" FieldList "}" | "(" TypeList ")" | ("=" Literal))?

TraitDef      ::= Attribute* "trait" Identifier GenericParams?
                  (":" TypeBound ("+" TypeBound)*)? "{" TraitItem* "}"
ImplBlock     ::= "impl" GenericParams? Type
                  ("for" Type)? "{" ImplItem* "}"

Type          ::= PrimitiveType | NamedType | OwnType | RefType | RefMutType
                | SliceType | ArrayType | TupleType | FnType | DynTrait | NeverType
OwnType       ::= "own" "<" Type ">"
RefType       ::= "ref" "<" Type ">"
RefMutType    ::= "refmut" "<" Type ">"
NullableType  ::= Type "|" "null"
SliceType     ::= "[" Type "]"
ArrayType     ::= "[" Type ";" ConstExpr "]"
TupleType     ::= "(" (Type ("," Type)*)? ")"
DynTrait      ::= "dyn" TypeBound ("+" TypeBound)*

Block         ::= "{" Stmt* Expr? "}"
Stmt          ::= LetStmt | ConstStmt | ExprStmt | Item
LetStmt       ::= "let" Pattern (":" Type)? ("=" Expr)? ";"
ConstStmt     ::= "const" Pattern (":" Type)? "=" Expr ";"

Expr          ::= LitExpr | PathExpr | BlockExpr | IfExpr | MatchExpr
                | LoopExpr | WhileExpr | ForExpr | ReturnExpr
                | BreakExpr | ContinueExpr | BinaryExpr | UnaryExpr
                | CallExpr | MethodCallExpr | FieldExpr | IndexExpr
                | RangeExpr | CastExpr | Closure | StructExpr | TupleExpr
                | ArrayExpr | MacroCall | UnsafeBlock | AssignExpr

Pattern       ::= LitPat | IdentPat | TuplePat | StructPat | EnumPat
                | SlicePat | RangePat | WildcardPat | OrPat | GuardPat

GenericParams ::= "<" GenericParam ("," GenericParam)* ","? ">"
GenericParam  ::= Identifier (":" TypeBound ("+" TypeBound)*)?
                | "const" Identifier ":" Type

TypeBound     ::= TraitPath | "?" "Sized"
```

---

## 23. Precedence Table

From highest to lowest:

| Precedence | Operators |
|------------|-----------|
| 15 | Field access `.`, method call `.()`, index `[]` |
| 14 | Unary `-`, `!`, `~`, `&` (borrow), `*` (deref), `as` |
| 13 | `*`, `/`, `%` |
| 12 | `+`, `-` |
| 11 | `<<`, `>>` |
| 10 | `&` (bitwise AND) |
| 9 | `^` |
| 8 | `\|` |
| 7 | `..`, `..=` (range) |
| 6 | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| 5 | `&&` |
| 4 | `\|\|` |
| 3 | `?` |
| 2 | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `\|=`, `^=`, `<<=`, `>>=` |
| 1 | `return`, `break`, `continue`, closure `=>` |

All binary operators are left-associative except assignment (`=`, `+=`, etc.) which is
right-associative.
