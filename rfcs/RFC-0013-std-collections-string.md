# RFC-0013 — Std: Collections and String

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0011, RFC-0012 |
| Module paths | `std::collections`, `std::string`, `std::fmt` |

## Summary

Defines the standard collection types, string types, and formatting infrastructure. All
types follow the ownership model: `push()` silently moves, `get()` returns borrows,
`remove()` returns owned values. The naming teaches ownership through the API itself.

## `Vec<T>` — Owned Growable Array

```typescript
class Vec<T> {
  static new(): own<Vec<T>>;
  static with_capacity(n: usize): own<Vec<T>>;

  // Mutation — all take refmut<Vec<T>> implicitly via method receiver
  push(v: own<T>): void;                  // SILENT MOVE — v consumed, no annotation needed
  pop(): Option<own<T>>;                  // moves element out of vec
  insert(i: usize, v: own<T>): void;      // O(n) shift
  remove(i: usize): own<T>;              // O(n) shift, moves out
  swap_remove(i: usize): own<T>;         // O(1), changes order
  retain(f: ref<T> -> bool): void;        // keep elements where f returns true
  clear(): void;                          // drops all elements
  truncate(len: usize): void;
  extend(iter: own<impl IntoIterator<Item=own<T>>>): void;
  drain(range): DrainIter<T>;             // owning iterator, shrinks vec

  // Access — borrow-based
  get(i: usize): Option<ref<T>>;
  get_mut(i: usize): Option<refmut<T>>;
  first(): Option<ref<T>>;
  last(): Option<ref<T>>;

  // Info
  len(): usize;
  is_empty(): bool;
  capacity(): usize;

  // Conversion
  into_boxed_slice(own<Vec<T>>): own<Box<[T]>>;
}
// Deref<Target=[T]> — all slice methods available on Vec
// IntoIterator: into_iter() consuming, iter() borrowing, iter_mut() mut borrowing
```

**Key design:** `push()` silently moves `T`. The borrow checker catches use-after-push
with a clear error. The error message teaches ownership; the API surface does not.

## `[T]` — Slice (Unsized Borrow)

Always used as `ref<[T]>` (fat pointer: `ptr + len`). Methods defined on slice are
available on `Vec<T>`, `StaticArray<T,N>`, `Box<[T]>` through `Deref`:

```typescript
// On ref<[T]>:
len(): usize
is_empty(): bool
first(): Option<ref<T>>
last(): Option<ref<T>>
get(i: usize): Option<ref<T>>
contains(v: ref<T>): bool              // T: PartialEq
starts_with(s: ref<[T]>): bool
ends_with(s: ref<[T]>): bool
split_at(i: usize): (ref<[T]>, ref<[T]>)
chunks(n: usize): Iterator<ref<[T]>>
windows(n: usize): Iterator<ref<[T]>>
iter(): Iterator<ref<T>>

// On refmut<[T]>:
sort(): void                           // T: Ord
sort_by(f: (ref<T>, ref<T>) -> Ordering): void
sort_unstable(): void                  // T: Ord, faster, no stability guarantee
swap(i: usize, j: usize): void
reverse(): void
fill(v: T): void                       // T: Copy
iter_mut(): Iterator<refmut<T>>

// Searching (ref<[T]>):
binary_search(v: ref<T>): Result<usize, usize>   // T: Ord; Ok=found, Err=insert point
partition_point(f: ref<T> -> bool): usize
```

## `StaticArray<T, N>` — Fixed-Size Stack Array

```typescript
type StaticArray<T, N: usize> = [T; N];
// N must be a compile-time constant (Sema enforces this)
// Copy if T: Copy (entire array bitwise-copied as a unit)
// Deref<Target=[T]> — all slice methods available
// Indexing is bounds-checked; out-of-bounds panics
```

## `HashMap<K, V>` — Hash Table

Requires `K: Hash + Eq`.

```typescript
class HashMap<K, V> {
  static new(): own<HashMap<K,V>>;
  static with_capacity(n: usize): own<HashMap<K,V>>;

  insert(k: own<K>, v: own<V>): Option<own<V>>; // returns old value if key existed
  get(k: ref<K>): Option<ref<V>>;               // BORROWED — map still owns V
  get_mut(k: ref<K>): Option<refmut<V>>;
  remove(k: ref<K>): Option<own<V>>;            // MOVES V out — caller now owns it
  contains_key(k: ref<K>): bool;
  entry(k: own<K>): Entry<K,V>;                 // insert-or-update pattern

  len(): usize;
  is_empty(): bool;
  clear(): void;

  keys(): Iterator<ref<K>>;
  values(): Iterator<ref<V>>;
  values_mut(): Iterator<refmut<V>>;
  // into_iter() -> Iterator<(own<K>, own<V>)>  consuming
  // iter()      -> Iterator<(ref<K>, ref<V>)>  borrowing
}
```

**Key design:** `get()` borrows (map retains ownership), `remove()` moves (caller takes
ownership). The naming teaches ownership without any annotation.

### `Entry<K,V>` API

```typescript
// Insert if absent
map.entry(key).or_insert(default_value);

// Lazy insert
map.entry(key).or_insert_with(|| compute());

// Modify existing or insert default
map.entry(key).and_modify(|v| { v.count += 1 }).or_insert(0);
```

## `BTreeMap<K, V>` — Sorted Hash Table

Requires `K: Ord`. Same `get`/`remove`/`insert` ownership semantics as `HashMap`.
Additional methods:

```typescript
range(range): Iterator<(ref<K>, ref<V>)>;     // iterate over a key range
first_key_value(): Option<(ref<K>, ref<V>)>;
last_key_value(): Option<(ref<K>, ref<V>)>;
pop_first(): Option<(own<K>, own<V>)>;         // removes and returns first entry
pop_last(): Option<(own<K>, own<V>)>;
```

## `HashSet<T>` and `BTreeSet<T>`

```typescript
class HashSet<T: Hash + Eq> {
  insert(v: own<T>): bool;           // true if not already present
  contains(v: ref<T>): bool;
  remove(v: ref<T>): bool;           // drops element; returns true if was present
  take(v: ref<T>): Option<own<T>>;   // removes and returns owned element
  // Set operations: union, intersection, difference, symmetric_difference
  // All return borrowing iterators — no ownership transfer
}
```

## `VecDeque<T>` — Ring Buffer Deque

```typescript
class VecDeque<T> {
  push_front(v: own<T>): void;
  push_back(v: own<T>): void;
  pop_front(): Option<own<T>>;
  pop_back(): Option<own<T>>;
  front(): Option<ref<T>>;
  back(): Option<ref<T>>;
  // Same ownership semantics as Vec for all operations
}
```

## `String` — Owned UTF-8

```typescript
class String {
  static new(): own<String>;
  static from(s: ref<str>): own<String>;         // allocates, copies bytes
  static with_capacity(n: usize): own<String>;

  push_str(s: ref<str>): void;                   // append str slice in place
  push(c: char): void;                           // append single char
  as_str(ref<String>): ref<str>;
  as_bytes(ref<String>): ref<[u8]>;
  into_bytes(own<String>): own<Vec<u8>>;         // consumes String
  into_boxed_str(own<String>): own<Box<str>>;

  len(): usize;
  is_empty(): bool;
  capacity(): usize;
  clear(): void;
  truncate(n: usize): void;                      // panics if not on char boundary

  // + operator: consumes left String, borrows right str
  // Deref<Target=str> — all str methods available on String
}
```

**`+` operator:** `a + b` where `a: own<String>`, `b: ref<str>` — appends `b` to `a`'s
buffer, returns `own<String>`. Does not allocate a new buffer in the common case. String
literals are `ref<str>` with static lifetime (Wasm data segment).

Template literals `` `Hello ${name}` `` desugar to `format!("Hello {}", name)`.

## `str` — Borrowed UTF-8 Slice

Unsized type, always `ref<str>` (fat pointer: `ptr + len`). Key methods:

```typescript
len(): usize
is_empty(): bool
contains(ref<str>): bool
starts_with(ref<str>): bool
ends_with(ref<str>): bool
find(ref<str>): Option<usize>
trim(): ref<str>           // sub-slice, zero allocation
trim_start(): ref<str>
trim_end(): ref<str>
split(sep: ref<str>): Iterator<ref<str>>     // zero allocation
splitn(n: usize, sep: ref<str>): Iterator<ref<str>>
lines(): Iterator<ref<str>>
chars(): Iterator<char>                      // Unicode scalar values
bytes(): Iterator<u8>
to_string(): own<String>                     // allocates
to_uppercase(): own<String>                  // allocates
to_lowercase(): own<String>                  // allocates
repeat(n: usize): own<String>                // allocates
parse::<T>(): Result<own<T>, ParseError>     // T: FromStr
```

## `fmt` Module

### Formatter and traits

```typescript
trait Display { fn fmt(ref<Self>, refmut<Formatter>): Result<void, FmtError>; }
trait Debug   { fn fmt(ref<Self>, refmut<Formatter>): Result<void, FmtError>; }
trait LowerHex, UpperHex, Binary, Octal, Pointer // formatting specifiers
```

`Formatter` wraps a `refmut<dyn Write>` sink. `write_str()`, `write_char()`, `write_fmt()`
push bytes directly without per-call heap allocation.

### Macros

```typescript
// format!() — allocates exactly one String (single-pass or two-pass sizing)
const s = format!("Hello, {}! You are {} years old.", name, age); // own<String>

// write!() — pushes to any Write implementor without allocating
write!(buf, "{}: {}", key, value);

// writeln!() — same as write! but appends '\n'

// println!() / print!() — WASI fd_write to fd 1 (stdout)
println!("Status: {}", status);

// eprintln!() / eprint!() — WASI fd_write to fd 2 (stderr)
eprintln!("Error: {}", e);

// Format specifiers:
// {}      — Display
// {:?}    — Debug
// {:#?}   — Debug pretty-print
// {:x}    — LowerHex
// {:X}    — UpperHex
// {:b}    — Binary
// {:o}    — Octal
// {:e}    — LowerExp (scientific notation)
// {:width$} — dynamic width padding
// {:.prec$} — dynamic precision
```

## `Result<T,E>` and `Option<T>`

```typescript
enum Result<T, E> {
  Ok(own<T>),
  Err(own<E>),
}

// Key methods:
unwrap(): own<T>                              // panics on Err
unwrap_or(default: own<T>): own<T>
unwrap_or_else(f: own<E> -> own<T>): own<T>
map(f: own<T> -> own<U>): Result<U,E>        // transforms Ok value
map_err(f: own<E> -> own<F>): Result<T,F>   // transforms Err value
and_then(f: own<T> -> Result<U,E>): Result<U,E>
or_else(f: own<E> -> Result<T,F>): Result<T,F>
ok(): Option<own<T>>                          // drops Err
err(): Option<own<E>>                         // drops Ok
is_ok(): bool
is_err(): bool

enum Option<T> {
  Some(own<T>),
  None,
}

unwrap(): own<T>
unwrap_or(default: own<T>): own<T>
map(f: own<T> -> own<U>): Option<U>
and_then(f: own<T> -> Option<U>): Option<U>
or_else(f: () -> Option<T>): Option<T>
as_ref(): Option<ref<T>>                     // borrow without consuming
as_mut(): Option<refmut<T>>
take(refmut<Option<T>>): Option<own<T>>      // replaces with None, returns old value
replace(refmut<Option<T>>, v: own<T>): Option<own<T>>
ok_or(e: own<E>): Result<T,E>
zip(other: Option<own<U>>): Option<(own<T>, own<U>)>
flatten(): Option<T>                         // Option<Option<T>> -> Option<T>
is_some(): bool
is_none(): bool
```

## `?` Operator Desugar

```typescript
// On Result<T,E> (function returns Result<R,F> where F: From<E>):
expr?
// desugars to:
match expr {
  Ok(v) => v,
  Err(e) => return Err(From::from(e)),  // e is moved into From::from
}

// On Option<T> (function returns Option<R>):
expr?
// desugars to:
match expr {
  Some(v) => v,
  None => return None,
}
```
