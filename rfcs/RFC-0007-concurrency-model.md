# RFC-0007 — Concurrency Model

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0005, RFC-0006 |
| Wasm proposals | Threads, Atomics, Bulk Memory (RFC-0004) |
| Flang reference | `flang/lib/Optimizer/Transforms/` (`fir.do_concurrent` lowering) |

## Summary

This RFC specifies the concurrency model in full detail: how `task.spawn` and channel
operations work, how ownership transfers across thread boundaries, how the lock-free ring
buffer is laid out in linear memory, how the concurrency lowering pass selects platform
thread primitives, and the static stack-size analysis for spawned threads.

## Design Principles

1. **Concurrency is ownership transfer** — a spawned task owns its captures; the spawner
   does not retain any reference to them after `task.spawn` returns.
2. **Channels are the only cross-thread communication mechanism** — no shared mutable state
   except through `mutex`-protected values.
3. **All synchronisation primitives lower to Wasm atomics or POSIX equivalents** — no
   runtime scheduler, no green threads, no async runtime heap.
4. **Thread stacks are statically sized** — determined by call-graph analysis before
   thread creation; no dynamic stack growth.

## Channel Memory Layout

A channel of type `T` with capacity `N` occupies a contiguous region in shared linear
memory:

```
Offset   Size   Field
──────   ────   ─────────────────────────────────────────────
0        4      head: i32         atomic read index (recv increments)
4        4      tail: i32         atomic write index (send increments)
8        4      capacity: i32     fixed at chan.make time
12       4      ref_count: i32    atomic; 2 = both tx and rx alive
16       N×s    slots[N]: T       contiguous slot array (s = sizeof(T))
```

Total size: `16 + N × sizeof(T)`, aligned to `max(8, alignof(T))`.

The channel header is allocated in shared linear memory by `chan.make` and freed when
`ref_count` reaches zero (both `tx` and `rx` dropped).

## `chan.send` Lowering

```
chan.send(!chan.tx<T>, !own.val<T>) → ()
```

Wasm instruction sequence:

```wasm
;; 1. Spin-wait until channel is not full
loop $retry
  local.get $tail
  i32.atomic.load (chan_base + 0)     ;; load head (atomic)
  local.get $tail
  i32.sub
  local.get $capacity
  i32.lt_u
  br_if $not_full
  memory.atomic.wait32 (chan_base + 0) $head_expected -1  ;; sleep
  br $retry
$not_full:

;; 2. Compute slot address
;; slot_addr = chan_base + 16 + (tail % capacity) * sizeof(T)
local.get $tail
local.get $capacity
i32.rem_u
i32.const sizeof_T
i32.mul
i32.const (chan_base + 16)
i32.add
local.set $slot_addr

;; 3. Copy value into slot (memory.copy = bulk-memory proposal)
local.get $slot_addr
local.get $val_ptr
i32.const sizeof_T
memory.copy

;; 4. Advance tail (release)
i32.const (chan_base + 4)
i32.const 1
i32.atomic.rmw.add

;; 5. Wake one waiting receiver
i32.const (chan_base + 4)   ;; notify on tail address
i32.const 1
memory.atomic.notify
```

**Ownership:** The `!own.val<T>` operand is consumed by step 3 (`memory.copy` into slot).
No `own.drop` is emitted for the sender. Ownership has been transferred physically into the
channel slot. The borrow checker sees the `chan.send` as the sole consuming use of the
`!own.val`.

## `chan.recv` Lowering

```
chan.recv(!chan.rx<T>) → !own.val<T>
```

Wasm instruction sequence:

```wasm
;; 1. Wait until channel is not empty
loop $wait
  i32.atomic.load (chan_base + 4)     ;; load tail (atomic)
  i32.atomic.load (chan_base + 0)     ;; load head (atomic)
  i32.ne
  br_if $has_data
  memory.atomic.wait32 (chan_base + 4) $tail_expected -1
  br $wait
$has_data:

;; 2. Compute slot address
;; slot_addr = chan_base + 16 + (head % capacity) * sizeof(T)
i32.atomic.load (chan_base + 0)      ;; load head
local.get $capacity
i32.rem_u
i32.const sizeof_T
i32.mul
i32.const (chan_base + 16)
i32.add
local.set $slot_addr

;; 3. Allocate new own.val in receiver frame
;; (own.alloc → alloca sizeof_T bytes in receiver's stack frame)
local.set $recv_ptr  ;; pointer to new !own.val<T>

;; 4. Copy slot → receiver frame
local.get $recv_ptr
local.get $slot_addr
i32.const sizeof_T
memory.copy

;; 5. Advance head (release)
i32.const (chan_base + 0)
i32.const 1
i32.atomic.rmw.add

;; 6. Wake one waiting sender
i32.const (chan_base + 0)   ;; notify on head address
i32.const 1
memory.atomic.notify
```

The returned `!own.val<T>` is now owned by the receiver.

## `task.spawn` Lowering (Wasm)

```
task.spawn(!own.val<C1>, !own.val<C2>, ...) { body } → !task.handle
```

Step-by-step:

### Step 1 — Closure struct materialization

The HIR builder emits a struct type `ClosureN` containing one field per captured value:

```
struct ClosureN {
  c1: C1,   // sizeof(C1) bytes
  c2: C2,   // sizeof(C2) bytes
  ...
  result: R,           // result slot (written by task before completion)
  done_flag: i32,      // atomic: 0 = running, 1 = done
}
```

The closure struct is allocated in the **thread stack arena** (RFC-0008). Its address is
passed as the thread argument.

### Step 2 — Copy captures into closure

For each captured `!own.val<Ci>`:

```wasm
local.get $closure_ptr_offset_i
local.get $ci_ptr
i32.const sizeof_Ci
memory.copy
```

After this, the `!own.val<Ci>` in the spawner's frame is consumed (no `own.drop` emitted —
ownership transferred to closure struct).

### Step 3 — Allocate thread stack

```
stack_ptr = arena.alloc(static_stack_size)
```

`static_stack_size` is determined by static call-graph analysis (see below).

### Step 4 — Start thread

```wasm
local.get $thread_fn_ptr    ;; pointer to emitted task entry function
local.get $closure_ptr
call $wasi_thread_start     ;; imported WASI function
local.set $thread_id
```

The task entry function (`thread_fn_ptr`) is a compiler-emitted Wasm function that:
1. Unpacks the closure struct
2. Executes the task body with the unpacked captures
3. Writes the result into `closure.result`
4. Sets `closure.done_flag` to 1 (atomic store, release)
5. Calls `memory.atomic.notify` on `done_flag` to wake the joiner

### Step 5 — Return handle

The `!task.handle` is a struct `{ thread_id: i32, closure_ptr: i32 }` stored in the
spawner's stack frame.

## `task.join` Lowering

```wasm
;; 1. Wait for task completion
local.get $done_flag_ptr
i32.const 0             ;; expected value (not done)
i64.const -1            ;; timeout: infinite
memory.atomic.wait32    ;; blocks until done_flag != 0

;; 2. Copy result from closure into caller frame
local.get $result_ptr   ;; destination: caller's stack frame
local.get $closure_result_ptr
i32.const sizeof_R
memory.copy

;; 3. Free thread stack
local.get $closure_ptr
call $arena_free
```

## Static Stack Size Analysis

Before `task.spawn` lowering, a pre-pass computes the maximum stack depth reachable from
each task body using call-graph analysis:

1. Build a call graph over all functions reachable from the task body
2. Compute the maximum stack frame size along any call chain (sum of `alloca` sizes in
   each frame, including all `own.val` allocations)
3. Add a safety margin of 4 KiB for alignment and callee-save registers
4. The result is the `static_stack_size` for that task

Recursive call cycles are detected and rejected with a compile error — recursive tasks
with unbounded stack depth are not supported in the static allocation model.

This is analogous to `fir.do_concurrent` stack frame analysis in Flang.

## `mutex.lock` / `mutex.unlock`

A mutex is a single `i32` in shared linear memory, initialized to `0` (unlocked).

### Lock

```wasm
loop $retry
  i32.const $mutex_addr
  i32.const 0             ;; expected: unlocked
  i32.const 1             ;; replacement: locked
  i32.atomic.rmw.cmpxchg  ;; returns old value
  i32.const 0
  i32.eq
  br_if $acquired         ;; if old == 0, we got the lock
  i32.const $mutex_addr
  i32.const 1             ;; expected: still locked
  i64.const -1            ;; timeout: infinite
  memory.atomic.wait32    ;; sleep until notified
  br $retry
$acquired:
```

### Unlock

```wasm
i32.const $mutex_addr
i32.const 0
i32.atomic.store          ;; release lock
i32.const $mutex_addr
i32.const 1               ;; wake one waiter
memory.atomic.notify
```

## Native Target Concurrency Lowering

For non-Wasm targets, the same HIR ops lower to POSIX or Win32 equivalents:

| Op | POSIX | Win32 |
|---|---|---|
| `task.spawn` | `pthread_create` | `CreateThread` |
| `task.join` | `pthread_join` | `WaitForSingleObject` |
| `chan.send` | C11 `atomic_fetch_add` + `futex`/`sem_wait` | `InterlockedAdd` + `WaitOnAddress` |
| `chan.recv` | Same | Same |
| `mutex.lock` | `pthread_mutex_lock` | `AcquireSRWLockExclusive` |
| `mutex.unlock` | `pthread_mutex_unlock` | `ReleaseSRWLockExclusive` |

On native targets, thread stacks are allocated by the OS (`pthread_attr_setstacksize`
with the statically computed size), not from a linear memory arena.
