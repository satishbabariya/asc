# RFC-0012 — Std: Memory Module

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0011 |
| Module paths | `std::mem`, `std::rc` |

## Summary

Defines heap-allocated smart pointer types, interior mutability wrappers, and low-level
memory primitives. All types in `std::mem` are auto-imported except `Rc<T>` and `Weak<T>`
which live in `std::rc` and require explicit import.

## `Box<T>` — Single Heap Owner

```typescript
class Box<T> {
  static new(v: own<T>): own<Box<T>>;
  into_inner(own<Box<T>>): own<T>;       // moves T out, frees allocation
  as_ref(ref<Box<T>>): ref<T>;
  as_mut(refmut<Box<T>>): refmut<T>;
  static leak(own<Box<T>>): ref<T>;      // 'static ref — intentional leak
}
// Deref<Target=T>, DerefMut<Target=T>
// Send if T: Send, Sync if T: Sync
// Drop: runs T destructor + frees allocation
```

Primary use: heap-allocating values that escape their function, or type-erasing
(`Box<dyn Trait>`).

## `Arc<T>` — Atomic Reference Count

```typescript
class Arc<T> {
  static new(v: own<T>): own<Arc<T>>;
  clone(ref<Arc<T>>): own<Arc<T>>;           // increments atomic refcount
  strong_count(ref<Arc<T>>): usize;
  weak_count(ref<Arc<T>>): usize;
  downgrade(ref<Arc<T>>): Weak<T>;
  static try_unwrap(own<Arc<T>>): Result<own<T>, own<Arc<T>>>;
                                              // Ok if refcount == 1
}
// Deref<Target=T> — shared, read-only through Arc
// Send+Sync if T: Send+Sync
// Drop: decrements count; drops T when count reaches 0
```

For mutation through `Arc`, wrap `T` in `Mutex<T>` or `RwLock<T>` (see RFC-0014).

## `Rc<T>` and `Weak<T>` — Single-Thread Reference Count

> **Requires explicit import:** `import { Rc, Weak } from 'std/rc'`
> **Lint:** `rc-in-hot-path` — compiler warning when `Rc` appears inside a loop or
> frequently-called function

```typescript
class Rc<T> {
  static new(v: own<T>): own<Rc<T>>;
  clone(ref<Rc<T>>): own<Rc<T>>;            // increments non-atomic refcount
  strong_count(ref<Rc<T>>): usize;
  weak_count(ref<Rc<T>>): usize;
  downgrade(ref<Rc<T>>): Weak<T>;           // must use Weak to break cycles
  static try_unwrap(own<Rc<T>>): Result<own<T>, own<Rc<T>>>;
}
// NOT Send, NOT Sync
// Deref<Target=T>
```

```typescript
class Weak<T> {
  upgrade(ref<Weak<T>>): Option<own<Rc<T>>>; // None if all Rc<T> dropped
  strong_count(ref<Weak<T>>): usize;
}
```

`Weak<T>` ships alongside `Rc<T>` — you cannot import `Rc` without `Weak` being visible.
Cycle detection remains the programmer's responsibility.

## `Arena<T>` — Bulk Same-Type Allocation

```typescript
class Arena<T> {
  static new(): own<Arena<T>>;
  static with_capacity(n: usize): own<Arena<T>>;
  alloc(refmut<Arena<T>>, v: own<T>): ref<T>;
  alloc_mut(refmut<Arena<T>>, v: own<T>): refmut<T>;
  len(ref<Arena<T>>): usize;
  // Drop: runs all T destructors, frees backing buffer in one free() call
}
```

Borrow checker rule: any `ref<T>` or `refmut<T>` issued by the arena is scoped to the
arena's lifetime. The arena cannot be dropped while any issued reference is live — enforced
automatically by the borrow checker.

Primary use: AST nodes, tree structures, graph nodes, any pattern where many objects share
a single lifetime.

## `Cell<T>` and `RefCell<T>` — Interior Mutability

```typescript
class Cell<T: Copy> {
  static new(v: T): Cell<T>;              // @copy, no heap
  get(ref<Cell<T>>): T;                   // returns a bitwise copy
  set(ref<Cell<T>>, v: T): void;          // replaces value (no borrow.mut needed)
  update(ref<Cell<T>>, f: T -> T): T;
}
// Send if T: Send, NOT Sync
```

```typescript
class RefCell<T> {
  static new(v: own<T>): own<RefCell<T>>;
  borrow(ref<RefCell<T>>): Ref<T>;        // panics if mut borrow active
  borrow_mut(ref<RefCell<T>>): RefMut<T>; // panics if any borrow active
  try_borrow(ref<RefCell<T>>): Result<Ref<T>, BorrowError>;
  try_borrow_mut(ref<RefCell<T>>): Result<RefMut<T>, BorrowMutError>;
  into_inner(own<RefCell<T>>): own<T>;
}
// Send if T: Send, NOT Sync
```

`Ref<T>` and `RefMut<T>` are RAII guards — dropping them releases the runtime borrow.
`RefMut<T>` implements `DerefMut<Target=T>`.

## `MaybeUninit<T>`

```typescript
class MaybeUninit<T> {
  static uninit(): MaybeUninit<T>;               // uninitialized — unsafe to read
  static new(v: own<T>): MaybeUninit<T>;
  write(refmut<MaybeUninit<T>>, v: own<T>): refmut<T>;
  unsafe assume_init(own<MaybeUninit<T>>): own<T>;
  unsafe assume_init_ref(ref<MaybeUninit<T>>): ref<T>;
}
```

Used internally by `Vec<T>` buffer management and FFI boundary code.

## `ManuallyDrop<T>`

```typescript
class ManuallyDrop<T> {
  static new(v: own<T>): ManuallyDrop<T>;   // suppresses auto-drop
  unsafe into_inner(own<ManuallyDrop<T>>): own<T>;
  unsafe drop(refmut<ManuallyDrop<T>>): void; // explicit manual drop
}
```

Used in union fields, FFI, and any place where the programmer takes explicit control of
destruction order.

## `ptr` Module — Raw Pointer Operations

```typescript
// Read/write through raw pointers — all unsafe
unsafe fn ptr_read<T>(src: *const T): own<T>;
unsafe fn ptr_write<T>(dst: *mut T, v: own<T>): void;
unsafe fn ptr_copy<T>(src: *const T, dst: *mut T, count: usize): void;
unsafe fn ptr_copy_nonoverlapping<T>(src: *const T, dst: *mut T, count: usize): void;

class NonNull<T> {
  static new(ptr: *mut T): Option<NonNull<T>>; // None if ptr is null
  unsafe static new_unchecked(ptr: *mut T): NonNull<T>;
  as_ptr(NonNull<T>): *mut T;
  unsafe as_ref(ref<NonNull<T>>): ref<T>;
  unsafe as_mut(refmut<NonNull<T>>): refmut<T>;
}
```

## Send/Sync Matrix

| Type | Send | Sync |
|---|---|---|
| `Box<T>` | if T: Send | if T: Sync |
| `Arc<T>` | if T: Send+Sync | if T: Send+Sync |
| `Rc<T>` | NO | NO |
| `Cell<T>` | if T: Send | NO |
| `RefCell<T>` | if T: Send | NO |
| `MaybeUninit<T>` | if T: Send | if T: Sync |
