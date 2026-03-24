# Decision — Vec::push() Silent Move

| Field | Value |
|---|---|
| Date | 2025 |
| Status | Accepted |
| Related RFC | RFC-0013 |

## Decision

**`Vec::push(v)` silently moves `T` into the vec. No explicit `push(move(x))` syntax.**

## Reasons

1. **Consistent with inference model.** Ownership inference silently marks moves at all
   function call sites. `push()` is just another function call — special syntax would be
   inconsistent.

2. **TypeScript-familiar call site.** `arr.push(x)` reads identically to TypeScript. The
   ownership difference is invisible at the call site, which is intentional.

3. **Error message is the teaching moment.** When a user writes `arr.push(x); use(x);`,
   the borrow checker emits a clear use-after-move error. The error teaches ownership; the
   API surface does not need to.

4. **Explicit syntax is noise without safety.** `push(move(x))` adds visual clutter at
   every call site without adding any safety guarantee — the borrow checker catches all
   violations regardless.

## Consequences

- Users must learn that `push()` consumes non-`@copy` arguments. The use-after-push error
  message is the primary teaching mechanism.
- For `@copy` types, `push()` bitwise-copies the value — no move, no annotation, identical
  to TypeScript semantics.

---

# Decision — Rc\<T\> in `std::rc`, Not Auto-Imported

| Field | Value |
|---|---|
| Date | 2025 |
| Status | Accepted |
| Related RFC | RFC-0012 |

## Decision

**`Rc<T>` lives in `std::rc`, requires `import { Rc, Weak } from 'std/rc'`, and triggers a
`lint(rc-in-hot-path)` warning when used inside a loop or frequently-called function.**

## Reasons

1. **Not a GC replacement.** `Rc<T>` allows multiple owners but does not eliminate
   ownership discipline — it defers drop to runtime reference counting. TypeScript developers
   may reach for it as "GC lite", which is the wrong pattern.

2. **Genuine use cases exist.** Tree structures, graph algorithms, and certain UI patterns
   genuinely need shared ownership. Fully excluding `Rc` forces awkward `Arena` workarounds
   or unsafe code for legitimate use cases.

3. **Explicit import = conscious choice.** Requiring an import makes the choice deliberate.
   A developer using `Rc` has had to think about it.

4. **`Weak<T>` co-location prevents the cycle trap.** Shipping `Weak<T>` in the same module
   means every developer who imports `Rc` immediately sees the tool for breaking cycles.
   You cannot discover `Rc` without `Weak` being visible.

5. **Lint catches misuse.** `rc-in-hot-path` warns when `Rc` appears in a tight loop — the
   most common misuse pattern.

## Consequences

- `Rc<T>` is **not** `Send` or `Sync`. It cannot cross `task.spawn` boundaries — compile
  error if attempted.
- Performance-critical code using `Rc` will need to migrate to `Arc + Mutex` or restructure
  to owned values when performance becomes a concern.
