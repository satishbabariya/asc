# RFC-0014 — Std: Concurrency and I/O

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0007, RFC-0011, RFC-0012 |
| Module paths | `std::thread`, `std::sync`, `std::channel`, `std::atomic`, `std::io`, `std::fs` |

## Summary

Defines the std wrappers over the concurrency primitives specified in RFC-0007, plus the
WASI I/O layer. All concurrency types enforce ownership-based safety at the type level;
I/O handles are RAII (file closes on drop, BufWriter flushes on drop).

## `thread` Module

```typescript
// Spawn a thread (wraps RFC-0007 task.spawn)
function spawn<F, R>(f: own<F>): Thread<R>
  where F: FnOnce() -> R + Send, R: Send;

class Thread<R> {
  join(own<Thread<R>>): Result<own<R>, PanicInfo>;  // blocks, returns result
  id(ref<Thread<R>>): ThreadId;
  is_finished(ref<Thread<R>>): bool;
}

class ThreadId { /* @copy, opaque */ }

// Current thread
function current_id(): ThreadId;
function sleep(duration: Duration): void;

// Scoped threads — can borrow from outer stack frame
function scope<'s, F: FnOnce(ref<Scope<'s>>)>(f: F): void;

class Scope<'s> {
  spawn<F, R>(ref<Scope>, f: own<F>): ScopedThread<'s, R>
    where F: FnOnce() -> R + Send + 's, R: Send;
  // scope() blocks until ALL spawned scoped threads have finished
  // This guarantees borrows of outer-scope data remain valid
}
```

**Scoped threads** allow borrowing from the enclosing stack frame because `scope()` is
guaranteed to block until all child threads exit. The borrow checker enforces the lifetime
constraint via the `'s` lifetime parameter.

## `sync` Module

### `Mutex<T>`

```typescript
class Mutex<T> {
  static new(v: own<T>): own<Mutex<T>>;
  lock(ref<Mutex<T>>): Result<MutexGuard<T>, PoisonError<T>>;
  try_lock(ref<Mutex<T>>): Result<MutexGuard<T>, TryLockError<T>>;
  into_inner(own<Mutex<T>>): Result<own<T>, PoisonError<T>>;
  is_poisoned(ref<Mutex<T>>): bool;
}

class MutexGuard<T> {
  // Deref<Target=T>, DerefMut<Target=T>
  // Drop: releases the lock
  // Poison: if the thread holding the lock panics, Mutex is marked poisoned.
  //         Future lock() calls return Err(PoisonError) until cleared.
}
// Mutex<T>: Send if T: Send, Sync if T: Send (always Sync if T: Send)
```

### `RwLock<T>`

```typescript
class RwLock<T> {
  static new(v: own<T>): own<RwLock<T>>;
  read(ref<RwLock<T>>): Result<RwLockReadGuard<T>, PoisonError<T>>;
  write(ref<RwLock<T>>): Result<RwLockWriteGuard<T>, PoisonError<T>>;
  try_read(ref<RwLock<T>>): Result<RwLockReadGuard<T>, TryLockError<T>>;
  try_write(ref<RwLock<T>>): Result<RwLockWriteGuard<T>, TryLockError<T>>;
  into_inner(own<RwLock<T>>): Result<own<T>, PoisonError<T>>;
}
// Multiple readers OR one writer at any time.
// RwLockReadGuard: Deref<Target=T>
// RwLockWriteGuard: Deref<Target=T> + DerefMut<Target=T>
```

### `Condvar`

```typescript
class Condvar {
  static new(): own<Condvar>;
  wait<T>(ref<Condvar>, guard: own<MutexGuard<T>>): Result<MutexGuard<T>, PoisonError<T>>;
  wait_while<T>(ref<Condvar>, guard: own<MutexGuard<T>>, condition: ref<T> -> bool)
    : Result<MutexGuard<T>, PoisonError<T>>;
  notify_one(ref<Condvar>): void;
  notify_all(ref<Condvar>): void;
}
```

### `Once`

```typescript
class Once {
  static new(): Once; // @copy
  call_once(ref<Once>, f: FnOnce()): void;   // f runs exactly once across all threads
  call_once_force(ref<Once>, f: FnOnce(ref<OnceState>)): void;
  is_completed(ref<Once>): bool;
}
```

Used for lazy static initialisation:

```typescript
static INSTANCE: Once = Once::new();
static mut VALUE: Option<own<Config>> = None;

function get_config(): ref<Config> {
  INSTANCE.call_once(|| { VALUE = Some(Config::load()); });
  VALUE.as_ref().unwrap()
}
```

### `Barrier`

```typescript
class Barrier {
  static new(n: usize): own<Barrier>;
  wait(ref<Barrier>): BarrierWaitResult;
  // Blocks until n threads have all called wait(), then all proceed simultaneously
}

class BarrierWaitResult { // @copy
  is_leader(): bool;  // exactly one thread per barrier phase is the leader
}
```

## `channel` Module

```typescript
// Bounded channel — wraps RFC-0007 chan dialect
function bounded<T: Send>(capacity: usize): (Sender<T>, Receiver<T>);

// Unbounded — dynamic capacity; linked list of fixed-size ring-buffer segments
function unbounded<T: Send>(): (Sender<T>, Receiver<T>);

class Sender<T> {
  send(ref<Sender<T>>, v: own<T>): Result<void, SendError<T>>;
  // SendError<T> wraps the unsent value back: SendError { value: own<T> }
  // i.e., if receiver dropped, you get your value back

  try_send(ref<Sender<T>>, v: own<T>): Result<void, TrySendError<T>>;
  // TrySendError::Full(own<T>) — channel full, value returned
  // TrySendError::Disconnected(own<T>) — receiver dropped, value returned

  // Clone: multiple producers allowed (atomic refcount on channel header)
  // Drop: decrements sender count; channel closed when count reaches 0
}

class Receiver<T> {
  recv(ref<Receiver<T>>): Result<own<T>, RecvError>;
  // RecvError — all senders dropped, channel empty

  try_recv(ref<Receiver<T>>): Result<own<T>, TryRecvError>;
  // TryRecvError::Empty — no message yet
  // TryRecvError::Disconnected — all senders dropped

  recv_timeout(ref<Receiver<T>>, timeout: Duration): Result<own<T>, RecvTimeoutError>;

  iter(ref<Receiver<T>>): Iterator<own<T>>;  // yields until channel closed

  // NOT Clone — single consumer per Receiver<T>
  // Drop: decrements receiver count
}
```

### `select!` Macro

```typescript
select! {
  v = rx1.recv() => {
    // handle v: own<T1>
  },
  v = rx2.recv() => {
    // handle v: own<T2>
  },
  _ = timeout(Duration::from_millis(100)) => {
    // timed out
  },
}
```

On Wasm: desugars to an `i32.atomic.wait` polling loop over multiple channel heads.
On native: uses a condition variable or `futex`-based wait.

## `atomic` Module

```typescript
enum Ordering { Relaxed, Acquire, Release, AcqRel, SeqCst }

class AtomicI32 {
  static new(v: i32): AtomicI32;    // NOT @copy despite being small
  load(ref<AtomicI32>, order: Ordering): i32;
  store(ref<AtomicI32>, v: i32, order: Ordering): void;
  swap(ref<AtomicI32>, v: i32, order: Ordering): i32;           // returns old
  fetch_add(ref<AtomicI32>, v: i32, order: Ordering): i32;      // returns old
  fetch_sub(ref<AtomicI32>, v: i32, order: Ordering): i32;
  fetch_and(ref<AtomicI32>, v: i32, order: Ordering): i32;
  fetch_or(ref<AtomicI32>, v: i32, order: Ordering): i32;
  fetch_xor(ref<AtomicI32>, v: i32, order: Ordering): i32;
  fetch_max(ref<AtomicI32>, v: i32, order: Ordering): i32;
  fetch_min(ref<AtomicI32>, v: i32, order: Ordering): i32;
  compare_exchange(
    ref<AtomicI32>,
    expected: i32, new: i32,
    success: Ordering, failure: Ordering
  ): Result<i32, i32>;              // Ok(old) on success, Err(actual) on failure
  compare_exchange_weak(/* same */): Result<i32, i32>;  // may spuriously fail
}
// Also: AtomicU32, AtomicI64, AtomicU64, AtomicBool, AtomicUsize
```

```typescript
class AtomicPtr<T> {
  static new(ptr: *mut T): AtomicPtr<T>;
  load(ref<AtomicPtr<T>>, order: Ordering): *mut T;   // raw pointer — unsafe to deref
  store(ref<AtomicPtr<T>>, ptr: *mut T, order: Ordering): void;
  swap(ref<AtomicPtr<T>>, ptr: *mut T, order: Ordering): *mut T;
  compare_exchange(
    ref<AtomicPtr<T>>,
    current: *mut T, new: *mut T,
    success: Ordering, failure: Ordering
  ): Result<*mut T, *mut T>;
}
```

**Ordering mapping to Wasm:**
- `Relaxed` → `memory_order_relaxed` (no fence)
- `Acquire` → `i32.atomic.load` with acquire semantics
- `Release` → `i32.atomic.store` with release semantics
- `SeqCst` → `i32.atomic.rmw` + `atomic.fence`

**Atomics are NOT `@copy`.** Despite being small, atomic types represent shared state —
ownership of the atomic cell itself is explicit.

## `io` Module — WASI I/O

```typescript
trait Read {
  fn read(refmut<Self>, buf: refmut<[u8]>): Result<usize, IoError>;
  fn read_exact(refmut<Self>, buf: refmut<[u8]>): Result<void, IoError>;
  fn read_to_end(refmut<Self>, buf: refmut<Vec<u8>>): Result<usize, IoError>;
  fn read_to_string(refmut<Self>, s: refmut<String>): Result<usize, IoError>;
}

trait Write {
  fn write(refmut<Self>, buf: ref<[u8]>): Result<usize, IoError>;
  fn write_all(refmut<Self>, buf: ref<[u8]>): Result<void, IoError>;
  fn write_fmt(refmut<Self>, fmt: Arguments): Result<void, IoError>;
  fn flush(refmut<Self>): Result<void, IoError>;
}

trait Seek {
  fn seek(refmut<Self>, pos: SeekFrom): Result<u64, IoError>;
  fn rewind(refmut<Self>): Result<void, IoError>;
  fn stream_position(refmut<Self>): Result<u64, IoError>;
}

enum SeekFrom { Start(u64), End(i64), Current(i64) }  // @copy

// Standard streams (module-level singletons)
function stdin(): Stdin;    // implements Read
function stdout(): Stdout;  // implements Write + Lock (thread-safe)
function stderr(): Stderr;  // implements Write + Lock (thread-safe)

class BufReader<R: Read> {
  static new(inner: own<R>): own<BufReader<R>>;
  static with_capacity(cap: usize, inner: own<R>): own<BufReader<R>>;
  into_inner(own<BufReader<R>>): own<R>;
  buffer(ref<BufReader<R>>): ref<[u8]>;
  // implements Read (buffered)
}

class BufWriter<W: Write> {
  static new(inner: own<W>): own<BufWriter<W>>;
  static with_capacity(cap: usize, inner: own<W>): own<BufWriter<W>>;
  into_inner(own<BufWriter<W>>): Result<own<W>, IntoInnerError<W>>;
  // Drop: flushes buffer, then drops inner W
  // implements Write (buffered — flushes automatically on drop)
}

class LineWriter<W: Write> {
  static new(inner: own<W>): own<LineWriter<W>>;
  // Auto-flushes on every newline character
}
```

## `fs` Module — WASI File System

```typescript
class File {
  static open(path: ref<str>): Result<own<File>, IoError>;    // read-only
  static create(path: ref<str>): Result<own<File>, IoError>;  // write, truncate
  static open_options(): OpenOptions;
  metadata(ref<File>): Result<Metadata, IoError>;
  set_len(ref<File>, size: u64): Result<void, IoError>;
  sync_all(ref<File>): Result<void, IoError>;
  // implements Read + Write + Seek
  // Drop: closes WASI fd
}

class OpenOptions {
  static new(): OpenOptions;       // @copy — builder pattern
  read(OpenOptions, v: bool): OpenOptions;
  write(OpenOptions, v: bool): OpenOptions;
  append(OpenOptions, v: bool): OpenOptions;
  truncate(OpenOptions, v: bool): OpenOptions;
  create(OpenOptions, v: bool): OpenOptions;
  create_new(OpenOptions, v: bool): OpenOptions;
  open(OpenOptions, path: ref<str>): Result<own<File>, IoError>;
}

class Path {
  static from_str(s: ref<str>): ref<Path>;    // zero-copy borrow as Path
  static new(s: own<String>): own<Path>;
  as_str(ref<Path>): ref<str>;
  join(ref<Path>, other: ref<str>): own<Path>;
  file_name(ref<Path>): Option<ref<str>>;
  file_stem(ref<Path>): Option<ref<str>>;     // file_name without extension
  extension(ref<Path>): Option<ref<str>>;
  parent(ref<Path>): Option<ref<Path>>;        // sub-borrow of self — no allocation
  is_absolute(ref<Path>): bool;
  is_relative(ref<Path>): bool;
}

// Convenience functions
function read(path: ref<str>): Result<own<Vec<u8>>, IoError>;
function read_to_string(path: ref<str>): Result<own<String>, IoError>;
function write(path: ref<str>, contents: ref<[u8]>): Result<void, IoError>;
function copy(from: ref<str>, to: ref<str>): Result<u64, IoError>;
function rename(from: ref<str>, to: ref<str>): Result<void, IoError>;
function remove_file(path: ref<str>): Result<void, IoError>;
function create_dir(path: ref<str>): Result<void, IoError>;
function create_dir_all(path: ref<str>): Result<void, IoError>;
function remove_dir(path: ref<str>): Result<void, IoError>;
function remove_dir_all(path: ref<str>): Result<void, IoError>;
function read_dir(path: ref<str>): Result<own<DirIter>, IoError>;
function metadata(path: ref<str>): Result<Metadata, IoError>;

class DirIter { /* implements Iterator<Item=Result<own<DirEntry>, IoError>> */ }

class DirEntry {
  path(ref<DirEntry>): own<Path>;
  file_name(ref<DirEntry>): own<String>;
  metadata(ref<DirEntry>): Result<Metadata, IoError>;
  file_type(ref<DirEntry>): Result<FileType, IoError>;
}

class Metadata { // @copy
  is_file(): bool;
  is_dir(): bool;
  is_symlink(): bool;
  len(): u64;
  modified(): Result<SystemTime, IoError>;
  accessed(): Result<SystemTime, IoError>;
  created(): Result<SystemTime, IoError>;
}
```

## `Error` Trait

```typescript
trait Error: Display {
  fn message(ref<Self>): ref<str>;
  fn source(ref<Self>): Option<ref<dyn Error>>;   // error cause chain
}

// Type-erased owned error — use for flexible error propagation
type AnyError = Box<dyn Error + Send + Sync>;

// Usage:
function parse_config(path: ref<str>): Result<own<Config>, AnyError> {
  const text = fs::read_to_string(path)?;  // IoError -> AnyError via From
  const config = Config::parse(text.as_str())?;  // ParseError -> AnyError via From
  Ok(config)
}
```
