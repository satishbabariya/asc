# asc Standard Library

The asc standard library provides foundational types, traits, collections, I/O, concurrency, and utility modules for AssemblyScript programs compiled with the `asc` compiler.

## Module Overview

| Module | Description | RFC |
|--------|-------------|-----|
| `core/traits` | Drop, Clone, Default, Deref | RFC-0011 |
| `core/cmp` | PartialEq, Eq, PartialOrd, Ord, Ordering | RFC-0011 |
| `core/hash` | Hash, Hasher, SipHash-1-3 | RFC-0011 |
| `core/fmt` | Display, Debug, Formatter | RFC-0011/0013 |
| `core/ops` | Add, Sub, Mul, Index, Range types | RFC-0011 |
| `core/iter` | Iterator, IntoIterator, adapters | RFC-0011 |
| `core/convert` | From, Into, TryFrom, TryInto | RFC-0011 |
| `core/option` | Option\<T\> | RFC-0013 |
| `result` | Result\<T,E\> | RFC-0013 |
| `string` | String, str methods | RFC-0013 |
| `vec` | Vec\<T\>, slice methods | RFC-0013 |
| `collections/*` | HashMap, BTreeMap, HashSet, BTreeSet, VecDeque | RFC-0013 |
| `collections/utils` | chunk, partition, flatten, zip, distinct | RFC-0017 |
| `mem/*` | Box, Arc, Rc, Arena, Cell, RefCell, ptr | RFC-0012 |
| `sync/*` | Mutex, RwLock, Condvar, Once, Barrier, Atomic | RFC-0014 |
| `thread/*` | spawn, Thread, channel | RFC-0014 |
| `io/*` | Read, Write, Seek, stdin/stdout/stderr | RFC-0014 |
| `fs/*` | File, Path, directory operations | RFC-0014/0019 |
| `json/*` | parse, stringify, JsonValue, Serialize/Deserialize | RFC-0016 |
| `encoding/*` | base64, hex, UTF-16, varint | RFC-0018 |
| `crypto/*` | SHA-256, HMAC, CSPRNG, Argon2 | RFC-0018 |
| `path/*` | POSIX and Windows path manipulation | RFC-0019 |
| `env/*` | Environment variables, .env loading | RFC-0019 |
| `config/*` | TOML, YAML, INI parsing | RFC-0019 |
| `async/*` | Semaphore, retry, debounce, TaskPool | RFC-0020 |

## Ownership Conventions

- `own<T>` — caller takes ownership, must drop or move
- `ref<T>` — shared immutable borrow
- `refmut<T>` — exclusive mutable borrow
- `@copy` structs — bitwise copied, no ownership tracking

## Import Syntax

```typescript
import { Vec, HashMap } from 'std/collections';
import { File } from 'std/fs';
import { parse } from 'std/json';
```

The prelude (`std/prelude`) is auto-imported and provides: Option, Result, Vec, String, Box, and all core traits.

## Platform Support

- **wasm32-wasi-threads** — primary target, uses WASI for I/O
- **x86_64-linux** — native target, uses POSIX syscalls
- **aarch64-linux** — native target
