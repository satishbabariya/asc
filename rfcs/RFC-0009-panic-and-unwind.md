# RFC-0009 — Panic and Unwind

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0005, RFC-0008 |
| Clang reference | `clang/lib/CodeGen/CGException.cpp` — Wasm EH lowering for C++ RAII |
| Wasm proposal | Exception Handling (phase 4 as of 2024) |

## Summary

Without correct unwind handling, a panic skips scope-exit `own.drop` calls and leaks
memory or leaves resources in invalid states. This RFC defines how panics are represented,
how the Wasm Exception Handling proposal is used to ensure every live `!own.val` is dropped
on all code paths (normal and panic), and how pathological cases (double-panic, in-flight
channel sends) are handled.

## Panic Representation

A panic is a Wasm exception with a dedicated tag:

```wasm
(tag $panic_tag (param i32))  ;; param = pointer to PanicInfo struct
```

`PanicInfo` layout in linear memory:

```
Offset  Size  Field
──────  ────  ──────────────────────────────────
0       4     message_ptr: i32   (UTF-8 string pointer)
4       4     message_len: i32
8       4     file_ptr: i32      (source file path)
12      4     file_len: i32
16      4     line: i32
20      4     column: i32
```

`$panic_tag` is exported from the runtime module and imported by all user modules.

## Panic Trigger

A panic is triggered by:

- Explicit `panic("message")` call in user code
- Implicit panics: out-of-bounds array access, integer overflow (in checked mode),
  null dereference, stack overflow (canary detection)

All panic triggers lower to:

```wasm
;; allocate PanicInfo in static data or stack
;; fill fields
throw $panic_tag (i32.const panic_info_ptr)
```

## Scope Wrapping

The **panic scope wrapping pass** runs after drop insertion (RFC-0008). It wraps every
function scope that contains at least one live `!own.val` in a Wasm `try`/`catch` block.

### Structure

```wasm
try
  ;; normal scope body (including normal-exit drops inserted by RFC-0008)
catch $panic_tag
  ;; unwind drops — same values, same LIFO order as normal-exit drops
  ;; but wrapped in drop-flag checks for conditional moves
  local.get $drop_flag_v1
  if
    call $drop_Foo (local.get $v1_ptr)
  end
  local.get $drop_flag_v2
  if
    call $drop_Bar (local.get $v2_ptr)
  end
  ;; rethrow to propagate panic to caller
  rethrow 0
end
```

The catch handler drops values in the **same LIFO order** as the normal exit path. This
ensures destructors observe a consistent state regardless of how the scope exits.

### Optimisation

If a scope has **no live `!own.val` at any point** (e.g., a scope containing only `@copy`
values or scalar ops), the `try`/`catch` wrapper is omitted. This is the common case for
hot numeric loops, which are not wrapped.

After Wasm EH lowering, LLVM's `simplifycfg` eliminates catch handlers that contain only
no-op drops (empty destructors + stack-only allocations that need no `free` call).

## Double-Panic Handling

If an `own.drop` call (destructor) itself triggers a panic while already unwinding, the
program is in an unrecoverable state. The compiler handles this as follows:

### Detection

A thread-local flag `in_unwind: i32` (stored in the thread-local storage segment) is set
to `1` before executing catch handler drops, and cleared to `0` after the catch handler
completes (just before `rethrow`).

### Action

If `$panic_tag` is caught while `in_unwind == 1`:

```wasm
;; check in_unwind flag
i32.const $in_unwind_tls_offset
i32.atomic.load thread_local
i32.const 1
i32.eq
if
  ;; double panic — abort immediately
  unreachable   ;; Wasm trap
end
```

This is equivalent to Rust's abort-on-double-panic behaviour. No further drops are
attempted. The process terminates with a Wasm trap.

## In-Flight Channel Send on Panic

**Scenario:** A sender panics after calling `own.move` into the channel slot but before
`i32.atomic.rmw.add` advances the tail pointer.

**Analysis:**

- The `!own.val` was consumed by `own.move` (the drop flag was cleared)
- `tail` has not advanced — the receiver cannot see the slot
- The slot contains valid data but is invisible to the receiver

**Resolution:** The sender's catch handler does **not** drop the slot value — it was
already moved and is no longer the sender's responsibility. The tail pointer is not
advanced, so the slot is logically empty from the receiver's perspective.

If the `chan.tx` handle is subsequently dropped (because the sender panicked before
`task.join`), `chan.tx.drop` decrements `ref_count`. When `ref_count` reaches zero, the
channel destructor runs and drops all values in slots `[head, tail)` (the visible,
unreceived values). The orphaned slot (invisible to receiver, not counted in `tail`) is
simply overwritten by the next sender.

## Channel Destructor

When both `tx` and `rx` handles are dropped (`ref_count` reaches 0), the channel
destructor runs:

```typescript
function chan_destroy<T>(chan_ptr: u32): void {
  const head = atomic_load(chan_ptr + 0);
  const tail = atomic_load(chan_ptr + 4);
  // drop all unreceived values
  for (let i = head; i != tail; i++) {
    const slot_ptr = chan_ptr + 16 + (i % capacity) * sizeof<T>();
    own_drop<T>(slot_ptr);
  }
  // free channel header + slot array
  free(chan_ptr);
}
```

## Top-Level Panic Handler

Panics that propagate to the top of a task's entry function are caught by a top-level
`try`/`catch` block emitted by the compiler for every task entry and the main function:

```wasm
try
  call $task_body
catch $panic_tag
  ;; write panic message to stderr
  call $write_panic_message
  ;; terminate: Wasm trap
  unreachable
end
```

On native targets, `unreachable` is replaced with a call to `abort()`. The process exits
with code 134 (SIGABRT) on POSIX systems.

## Interaction with Normal-Exit Drops

The panic scope wrapping pass runs **after** the drop insertion pass (RFC-0008). The two
passes interact as follows:

1. Drop insertion places `own.drop` ops on all **normal exit** paths (return, branch)
2. Panic scope wrapping places `own.drop` ops (with drop flag checks) in **catch handlers**
   for all **unwind exit** paths

This two-pass design avoids duplication: the normal drops and the unwind drops are
generated independently and are always consistent because both read from the same
`LivenessInfo` computed in RFC-0006 Pass 1.

## Reference

The `try`/`catch`/`rethrow` emission structure follows Clang's `CGException.cpp`,
specifically the `emitLandingPad` and `emitCleanupBlock` functions adapted for the Wasm EH
model. The key difference from DWARF EH (x86-64) is that Wasm EH uses explicit `catch`
instructions rather than DWARF unwind tables. The semantic model (RAII cleanup on any exit
path) is identical.
