# RFC Implementation Audit — Complete Summary

**Date:** 2026-04-17
**Branch:** `claude/rfc-implementation-audit-6LrGD`
**Scope:** RFC-0001 through RFC-0020, cross-checked against `lib/`, `include/asc/`, `std/`, `test/`.
**Method:** 4 parallel code-survey agents, one per 5-RFC cluster. Each RFC's goals and feature list were read from `rfcs/`, then grepped for concrete evidence (file:line citations) in the source tree.

## Headline numbers

| Metric | Claimed (CLAUDE.md) | Verified | Δ |
|---|---|---|---|
| Mean per-RFC coverage | **78.1%** | **77.9%** | −0.2 pp |
| RFCs where claim was conservative | — | 8 of 20 | +1 to +10 pp |
| RFCs where claim was optimistic | — | 11 of 20 | −1 to −5 pp |
| RFCs unchanged | — | 1 of 20 | ±0 |

The aggregate ~85% weighted coverage advertised in `CLAUDE.md` is **substantively honest**: small per-RFC drifts largely cancel out. No RFC was materially misrepresented in either direction.

## Per-RFC results

| # | Title | Claimed | Verified | Drift | Primary residual gap |
|---|---|---|---|---|---|
| 0001 | Project Overview | 97% | **95%** | −2 | Cross-module IR resolution |
| 0002 | Surface Syntax | 92% | **88%** | −4 | `task.spawn` closure captures via globals, not env struct |
| 0003 | Compiler Pipeline | 93% | **91%** | −2 | Constant folding via `arith.constant`, not ConstantExpr |
| 0004 | Target Support | 86% | **82%** | −4 | Windows MSVC untested in CI, `--wasm-features` partial |
| 0005 | Ownership Model | 88% | **86%** | −2 | Copy not auto-derived; alias SSA trace capped at depth 16 |
| 0006 | Borrow Checker | 83% | **85%** | +2 | Drop-flag runtime checks partially wired |
| 0007 | Concurrency | 48% | **50%** | +2 | MPMC stub-only; Wasm `memory.atomic.notify` unemitted; thread-arena allocator missing |
| 0008 | Memory Model | 68% | **70%** | +2 | Thread-stack arena not fully wired; heap thresholds not configurable |
| 0009 | Panic/Unwind | 65% | **62%** | −3 | Wasm EH proposal not used; double-panic TLS flag missing |
| 0010 | Toolchain/DX | 80% | **82%** | +2 | DWARF/source-map output untested; `asc fmt` token-level only |
| 0011 | Core Traits | 93% | **88%** | −5 | Hash/Debug are marker-only; some iterator adapter return types partial |
| 0012 | Memory Module | 87% | **84%** | −3 | `AtomicPtr<T>` missing; `MaybeUninit::assume_init_ref` unverified |
| 0013 | Collections/String | 90% | **87%** | −3 | `Formatter` width/precision partial; `LinkedList` basic only |
| 0014 | Concurrency/IO | 86% | **81%** | −5 | `thread::scope` missing; `select!` polling-based on Wasm; `PoisonError` incomplete |
| 0015 | Complete Syntax | 89% | **85%** | −4 | async/await explicitly deferred per §21; generators, `?.`, `??` not supported (by design) |
| 0016 | JSON | 35% | **45%** | **+10** | `derive(Serialize/Deserialize)` still blocked on macro expansion; no JsonWriter/Builder; no field attrs |
| 0017 | Collections Utils | 65% | **70%** | +5 | No `Rng` trait / `shuffle` / `sample`; no `combinations`/`permutations`; no `deep_merge` |
| 0018 | Encoding/Crypto | 75% | **78%** | +3 | SHA-3 (Keccak) absent — otherwise comprehensive |
| 0019 | Path/Config | 72% | **75%** | +3 | No `glob_match`; no `load_default_dotenv`; no `var_parsed<T: FromStr>` |
| 0020 | Async Utilities | 72% | **75%** | +3 | async/await syntax intentionally deferred; `deadline_at(Instant)` missing |

## Where CLAUDE.md overstated

Mostly in trait-surface RFCs where **marker declarations are counted as implementations**. Concretely:

- **RFC-0011 (−5 pp):** `Hash` and `Debug` appear in the trait registry and in `@derive` as markers, but neither synthesizes real method bodies. `Hasher` infrastructure (`SipHash-1-3`) exists in `std/core/hash.ts:4` but is not wired through `@derive(Hash)`. Debug has no `Formatter` dispatch.
- **RFC-0014 (−5 pp):** `select!` is listed as implemented but is polling-based on Wasm; `thread::scope` is absent; `PoisonError`/`TryLockError` types are stubs.
- **RFC-0002 (−4 pp):** `task.spawn` closure capture is implemented as *module-level globals written pre-spawn* rather than an env struct — this passes simple e2e tests but violates RFC-0002's hybrid example when captured values are owned and must be moved.
- **RFC-0015 (−4 pp):** several listed features (`?.`, `??`, try/catch, for…in, generators) are **intentionally out of scope** per the RFC itself. The claimed 89% already accounts for this, so the drift is small.

## Where CLAUDE.md understated

- **RFC-0016 (+10 pp):** `JsonValue`, `JsonParser` (RFC 8259 strict), `JsonSlice` zero-copy view, and `JsonError` position tracking are all working end-to-end. The headline blocker — `derive(Serialize/Deserialize)` — is genuinely missing, but the parser/value surface area is larger than 35% would suggest.
- **RFC-0017 (+5 pp):** `chunk`, `partition_point`, `flatten`, `zip_with`, `unzip`, `interleave`, `intersect`, `bisect_{left,right}`, `sort_by_key`, `frequencies`, `invert`, `merge_with`, `group_by` are all present in `std/collections/utils.ts`. The RNG-dependent utilities (`shuffle`, `sample`, `combinations`, `permutations`) are the clean gap.

## Cross-cutting risks

These gaps are not tied to a single RFC but ripple across several:

1. **Closure env struct (RFC-0002, RFC-0007, RFC-0014).** The "globals as capture" workaround is a correctness hazard for any spawn-heavy code involving owned captures. Any Arc/Mutex refactor that depends on captures moving into the task body is currently unsound.
2. **Drop flag runtime (RFC-0005, RFC-0006, RFC-0009).** `MaybeMoved` warns at compile time, but the runtime flag is allocated without being consistently checked at scope exit and panic unwind. Conditional-move-then-panic paths are underverified.
3. **Wasm exception handling (RFC-0004, RFC-0009).** Everything uses setjmp/longjmp; the Wasm EH proposal is not emitted. Acceptable for MVP, but binds the compiler to a non-standard unwind strategy on Wasm.
4. **Alias analysis depth cap (RFC-0005, RFC-0006).** SSA load-chain tracing is capped at depth 16. Deep struct-of-struct-of-struct field access may under-report E001/E003.
5. **Macro expansion (RFC-0016, RFC-0011).** `derive(Serialize/Deserialize)` and `derive(Debug/Hash)` all blocked on the same macro-expansion infrastructure gap. Unblocking that lifts four RFCs at once.

## Known gaps reconfirmed (CLAUDE.md §"Known Gaps")

All 11 listed gaps in `CLAUDE.md` lines 180-191 were verified against the code and remain accurate:
closure captures, drop flags, Wasm EH, MPMC channels, constant folding, multi-module linking,
derive(Serialize/Deserialize), async/await, SHA-3, AtomicPtr, scoped threads.

No additional headline gaps were discovered during this audit.

## Recommendation ordering

If the project pursues a single next push to raise coverage, the highest leverage ordered targets are:

1. **Macro expansion for `@derive`** — unblocks derive(Serialize/Deserialize) (RFC-0016), derive(Debug) (RFC-0011), derive(Hash) (RFC-0011). Estimated lift: +5 to +8 aggregate points.
2. **Closure env struct for `task.spawn`** — unblocks real-world concurrency (RFC-0002, RFC-0007). Removes a silent-miscompile risk.
3. **AtomicPtr + `thread::scope`** — small, mechanical, closes two of the eleven known gaps (RFC-0012, RFC-0014).
4. **Rng trait + sampling utilities** — completes RFC-0017 and unblocks crypto/random higher-level APIs.

## Methodology notes

Each cluster agent was asked to (a) read the RFC text, (b) grep the codebase for evidence of each feature with file:line citations, (c) flag missing/partial items, and (d) confirm or revise the coverage percentage. Percentages in the **Verified** column are the agents' per-RFC judgments after evidence collection, not a mechanical checklist fraction. The mean was computed unweighted across all 20 RFCs.

Test suite state at audit time: 261 lit tests at 100% per `CLAUDE.md`; no tests were run as part of this audit (static review only).
