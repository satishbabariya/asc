# RFC-0011 — Std: Core Traits

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0005, RFC-0006 |
| Module path | `std::core` (auto-imported) |

## Summary

Defines the foundational trait hierarchy the entire standard library is built on. All traits
live in `std::core` and are auto-imported into every module.

## Lifecycle Traits

### `Drop`

```typescript
trait Drop {
  fn drop(refmut<Self>): void;
}
```

Runs the destructor. Called by `own.drop`. **`Copy` and `Drop` are mutually exclusive** —
Sema rejects `@copy` on any type that implements `Drop`.

### `Copy`

Marker trait. Bitwise copy is safe. No destructor runs on copy. All numeric primitives
(`i8`–`i64`, `u8`–`u64`, `f32`, `f64`, `bool`, `char`, `usize`) are `Copy` by default.
`@copy` structs must have all-`Copy` fields.

### `Clone`

```typescript
trait Clone {
  fn clone(ref<Self>): own<Self>;
}
```

Explicit deep copy. Distinguished from `Copy`: `Clone` is always explicit (you call
`.clone()`); `Copy` is implicit (happens in any copy context). `derive(Clone)` is
auto-generated when all fields are `Clone`.

### `Send` and `Sync`

Marker traits encoding thread safety:

- `Send` — safe to **move** across a thread boundary (`task.spawn` requires this)
- `Sync` — safe to **share references** across threads (`ref<T>` is `Send` iff `T: Sync`)

Auto-derived: a type is `Send` if all its fields are `Send`. Never manually implemented for
safety-critical types.

## Comparison Traits

```typescript
trait PartialEq {
  fn eq(ref<Self>, ref<Self>): bool;
  fn ne(ref<Self>, ref<Self>): bool; // default: !self.eq(other)
}

trait Eq: PartialEq {} // adds reflexive/symmetric/transitive guarantee

trait PartialOrd: PartialEq {
  fn partial_cmp(ref<Self>, ref<Self>): Option<Ordering>;
}

trait Ord: Eq + PartialOrd {
  fn cmp(ref<Self>, ref<Self>): Ordering;
}
```

`Ordering` is a `@copy` enum: `Less`, `Equal`, `Greater`.

`f32`/`f64` implement `PartialEq` and `PartialOrd` but **not** `Eq` or `Ord` (NaN ≠ NaN).

## `Hash`

```typescript
trait Hash {
  fn hash(ref<Self>, refmut<Hasher>): void;
}
```

Required for `HashMap<K,V>` and `HashSet<T>` keys. Default hasher: SipHash-1-3.

## `Default`

```typescript
trait Default {
  fn default(): own<Self>;
}
```

Zero-value construction. Auto-derivable if all fields implement `Default`.

## `Sized`

Marker trait. All types are `Sized` unless explicitly `?Sized`. Unsized types (`str`,
`[T]`, `dyn Trait`) are always used behind a reference (fat pointer: `ptr + metadata`).

## Iterator Traits

```typescript
trait Iterator {
  type Item;
  fn next(refmut<Self>): Option<Item>;

  // Provided (implemented in terms of next):
  fn map<B>(own<Self>, f: own<T> -> own<B>): MapIter<Self, B>;
  fn filter(own<Self>, f: ref<T> -> bool): FilterIter<Self>;
  fn fold<B>(own<Self>, init: own<B>, f: (own<B>, own<T>) -> own<B>): own<B>;
  fn collect<C: FromIterator<T>>(own<Self>): own<C>;
  fn enumerate(own<Self>): EnumerateIter<Self>;
  fn zip<U>(own<Self>, other: own<U>): ZipIter<Self, U>;
  fn chain<U>(own<Self>, other: own<U>): ChainIter<Self, U>;
  fn take(own<Self>, n: usize): TakeIter<Self>;
  fn skip(own<Self>, n: usize): SkipIter<Self>;
  fn flat_map<B>(own<Self>, f: own<T> -> own<impl Iterator<Item=B>>): FlatMapIter<Self, B>;
  fn count(own<Self>): usize;
  fn any(own<Self>, f: ref<T> -> bool): bool;
  fn all(own<Self>, f: ref<T> -> bool): bool;
  fn find(own<Self>, f: ref<T> -> bool): Option<own<T>>;
  fn position(own<Self>, f: ref<T> -> bool): Option<usize>;
  fn max(own<Self>): Option<own<T>>; // T: Ord
  fn min(own<Self>): Option<own<T>>; // T: Ord
  fn sum(own<Self>): T;              // T: Add + Default
  fn product(own<Self>): T;          // T: Mul + Default
  fn last(own<Self>): Option<own<T>>;
  fn nth(own<Self>, n: usize): Option<own<T>>;
  fn peekable(own<Self>): Peekable<Self>;
}

trait IntoIterator {
  type Item;
  type IntoIter: Iterator<Item=Item>;
  fn into_iter(own<Self>): own<IntoIter>;
}

trait FromIterator<T> {
  fn collect<I: Iterator<Item=own<T>>>(iter: own<I>): own<Self>;
}
```

`for...of` desugars to `IntoIterator::into_iter()`. For `Vec<T>`, this **consumes** the
vec. Borrowing iteration requires explicit `.iter()` or `.iter_mut()` calls.

Collections implement three iterator modes:
- `into_iter()` — consumes collection, yields `own<T>`
- `iter()` — borrows collection, yields `ref<T>`
- `iter_mut()` — mutably borrows, yields `refmut<T>`

## Index Traits

```typescript
trait Index<Idx> {
  type Output;
  fn index(ref<Self>, Idx): ref<Output>;
}

trait IndexMut<Idx>: Index<Idx> {
  fn index_mut(refmut<Self>, Idx): refmut<Output>;
}
```

`[]` desugars to `index()` or `index_mut()`. Returned borrow is scoped to the
container borrow — enforced automatically by the borrow checker.

## Deref Traits

```typescript
trait Deref {
  type Target;
  fn deref(ref<Self>): ref<Target>;
}

trait DerefMut: Deref {
  fn deref_mut(refmut<Self>): refmut<Target>;
}
```

Deref coercions:
- `Box<T>` → `T`
- `Vec<T>` → `[T]` (slice)
- `String` → `str`

## Display and Debug

```typescript
trait Display {
  fn fmt(ref<Self>, refmut<Formatter>): Result<void, FmtError>;
}

trait Debug {
  fn fmt(ref<Self>, refmut<Formatter>): Result<void, FmtError>;
}
```

`Formatter` wraps a `refmut<dyn Write>` sink. `write_str()`, `write_char()`, `write_fmt()`
push bytes without per-call heap allocation.

## `From` and `Into`

```typescript
trait From<T> {
  fn from(v: own<T>): own<Self>;
}

trait Into<T> {
  fn into(own<Self>): own<T>;
}
// Blanket impl: if T: From<U>, then U: Into<T>
```

Used by the `?` operator for error type conversion (`From::from(e)`).

## Operator Traits

All arithmetic operators (`+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `<<`, `>>`) have
corresponding traits (`Add`, `Sub`, `Mul`, `Div`, `Rem`, `BitAnd`, `BitOr`, `BitXor`,
`Shl`, `Shr`). Each takes `own<Self>` and `own<Rhs>` and returns `own<Output>`.

`+=`, `-=` etc. correspond to `AddAssign`, `SubAssign` etc., taking `refmut<Self>` and
`own<Rhs>` and returning `void`.
