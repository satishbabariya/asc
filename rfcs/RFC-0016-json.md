# RFC-0016 — JSON Support

| Field       | Value                                              |
|-------------|----------------------------------------------------|
| Status      | Accepted                                           |
| Depends on  | RFC-0011, RFC-0012, RFC-0013, RFC-0014, RFC-0015   |
| Module path | `std::json`                                        |

## Summary

This RFC defines the complete JSON support story for the standard library. JSON handling
is split into three cooperating layers that coexist in `std::json`:

- **Layer 1 — `derive(Serialize, Deserialize)`** — primary API, 90% of use cases. Parses
  directly into a typed `own<T>` with no intermediate tree. Compile-time schema.
- **Layer 2 — `JsonValue` owned tree** — for dynamic / unknown-schema JSON. Full ownership
  semantics. Explicit `i64`, `u64`, `f64` number variants; no precision loss.
- **Layer 3 — `JsonSlice` zero-copy view** — for hot paths and protocol handlers. Borrows
  input bytes; zero allocation on parse when strings have no escape sequences. In `std::json`
  core (not a separate crate) because Wasm performance requires it.

**Format:** Strict RFC 8259 only. No JSON5, no comments, no trailing commas, no extensions.
Lenient parsing is not supported and not planned.

---

## 1. `JsonValue` — Owned Dynamic Tree

### 1.1 Type definition

```typescript
enum JsonValue {
  Null,
  Bool(bool),
  Int(i64),       // JSON integers that fit in i64
  Uint(u64),      // JSON integers > i64::MAX (positive only)
  Float(f64),     // JSON numbers containing '.' or 'e'/'E'
  Str(own<String>),
  Array(own<Vec<JsonValue>>),
  Object(own<Vec<(own<String>, JsonValue)>>),  // ordered — preserves insertion order
}
```

**Number variant selection during parsing:**
1. If the literal contains `.`, `e`, or `E` → `Float(f64)`
2. If negative or fits in `i64::MIN..=i64::MAX` → `Int(i64)`
3. If positive and > `i64::MAX` and fits in `u64::MAX` → `Uint(u64)`
4. If > `u64::MAX` → parse error (value out of range)

`Object` uses `Vec<(String, JsonValue)>` not `HashMap` to preserve key insertion order
(required by some APIs) and to allow duplicate keys (which RFC 8259 permits). Methods that
need lookup use linear scan; for large objects users should call `.into_map()` explicitly.

### 1.2 Parsing

```typescript
impl JsonValue {
  // Parse a JSON document from a string slice
  static parse(input: ref<str>): Result<own<JsonValue>, JsonError>;

  // Parse from any Read source (streaming — reads in 4KB chunks)
  static parse_reader<R: Read>(reader: refmut<R>): Result<own<JsonValue>, JsonError>;
}
```

### 1.3 Navigation — borrowing accessors

```typescript
impl JsonValue {
  // Type checks
  fn is_null(ref<Self>): bool;
  fn is_bool(ref<Self>): bool;
  fn is_number(ref<Self>): bool;   // true for Int, Uint, Float
  fn is_str(ref<Self>): bool;
  fn is_array(ref<Self>): bool;
  fn is_object(ref<Self>): bool;

  // Typed extractors — borrow, never consume
  fn as_bool(ref<Self>): Option<bool>;
  fn as_i64(ref<Self>): Option<i64>;   // None if Uint > i64::MAX or not a number
  fn as_u64(ref<Self>): Option<u64>;   // None if Int < 0 or not a number
  fn as_f64(ref<Self>): f64;           // always succeeds: Int/Uint cast lossily
  fn as_str(ref<Self>): Option<ref<str>>;
  fn as_array(ref<Self>): Option<ref<[JsonValue]>>;

  // Object key lookup — linear scan, O(n)
  fn get(ref<Self>, key: ref<str>): Option<ref<JsonValue>>;
  fn get_mut(refmut<Self>, key: ref<str>): Option<refmut<JsonValue>>;

  // Array index
  fn index(ref<Self>, i: usize): Option<ref<JsonValue>>;

  // [] operator — panics on wrong type or missing key (use get() for safe access)
  // Implements Index<ref<str>> and Index<usize>
}
```

### 1.4 Construction

```typescript
impl JsonValue {
  // Constructors
  static null(): JsonValue;
  static bool(v: bool): JsonValue;
  static int(v: i64): JsonValue;
  static uint(v: u64): JsonValue;
  static float(v: f64): JsonValue;
  static string(v: own<String>): JsonValue;
  static string_ref(v: ref<str>): JsonValue;    // allocates a copy
  static array(v: own<Vec<JsonValue>>): JsonValue;
  static object(v: own<Vec<(own<String>, JsonValue)>>): JsonValue;

  // Builder convenience
  static object_builder(): JsonObjectBuilder;
  static array_builder(): JsonArrayBuilder;
}

class JsonObjectBuilder {
  insert(refmut<Self>, key: ref<str>, val: JsonValue): refmut<JsonObjectBuilder>;
  build(own<Self>): own<JsonValue>;
}

class JsonArrayBuilder {
  push(refmut<Self>, val: JsonValue): refmut<JsonArrayBuilder>;
  build(own<Self>): own<JsonValue>;
}
```

### 1.5 Mutation

```typescript
impl JsonValue {
  // Object mutation
  fn insert(refmut<Self>, key: own<String>, val: JsonValue): Option<JsonValue>;
  fn remove(refmut<Self>, key: ref<str>): Option<JsonValue>;     // moves value out

  // Array mutation — same ownership semantics as Vec<T>
  fn push(refmut<Self>, val: JsonValue): void;
  fn pop(refmut<Self>): Option<JsonValue>;
}
```

### 1.6 Serialization

```typescript
impl JsonValue {
  fn to_json(ref<Self>): own<String>;
  fn to_json_pretty(ref<Self>, indent: usize): own<String>;
  fn write_json<W: Write>(ref<Self>, sink: refmut<W>): Result<void, IoError>;
  fn write_json_pretty<W: Write>(ref<Self>, sink: refmut<W>, indent: usize)
    : Result<void, IoError>;
}
// Also implements Display (compact JSON) and Debug (pretty-printed with type tags)
```

---

## 2. `JsonSlice` — Zero-Copy Borrowed View

Lives in `std::json` (same module as `JsonValue`). Borrows the input `ref<str>` — the
borrow checker ensures `JsonSlice` cannot outlive the input.

### 2.1 Type definition and parsing

```typescript
// JsonSlice is a view into a parsed JSON document.
// It borrows the original input bytes — zero heap allocation on parse
// UNLESS a string value contains escape sequences (e.g. \n, \uXXXX),
// in which case that string must be materialized to resolve the escapes.
struct JsonSlice {
  // internal: (type_tag, ptr, len) into original input bytes
  // NOT pub — always access through methods
}

impl JsonSlice {
  // Parse into a zero-copy view. Returns a JsonSlice that borrows `input`.
  // The returned JsonSlice has the same lifetime as `input`.
  static parse(input: ref<str>): Result<JsonSlice, JsonError>;
}
```

### 2.2 Accessor API

```typescript
impl JsonSlice {
  // Type checks (@copy — cheap to pass by value)
  fn is_null(JsonSlice): bool;
  fn is_bool(JsonSlice): bool;
  fn is_number(JsonSlice): bool;
  fn is_str(JsonSlice): bool;
  fn is_array(JsonSlice): bool;
  fn is_object(JsonSlice): bool;

  // Scalar extraction — parse from raw bytes on demand
  fn as_bool(JsonSlice): Option<bool>;
  fn as_i64(JsonSlice): Option<i64>;
  fn as_u64(JsonSlice): Option<u64>;
  fn as_f64(JsonSlice): Option<f64>;

  // String extraction:
  // Zero-copy if no escape sequences in the raw JSON string value.
  // Returns None if value is not a string.
  fn as_str(JsonSlice): Option<ref<str>>;

  // Always works for string values. Allocates if escape sequences present.
  fn as_str_owned(JsonSlice): Option<own<String>>;

  // Raw JSON bytes for this value (useful for partial re-parsing)
  fn raw_json(JsonSlice): ref<str>;

  // Object key lookup — returns a sub-slice into the same input
  fn get(JsonSlice, key: ref<str>): Option<JsonSlice>;

  // Array index — returns a sub-slice
  fn index(JsonSlice, i: usize): Option<JsonSlice>;

  // Iterate object key-value pairs (both are sub-slices)
  fn object_iter(JsonSlice): impl Iterator<Item=(JsonSlice, JsonSlice)>;

  // Iterate array elements (each is a sub-slice)
  fn array_iter(JsonSlice): impl Iterator<Item=JsonSlice>;

  // Materialize to owned — escape hatch if you need to store the value
  fn to_owned(JsonSlice): own<JsonValue>;

  // Deserialize directly into T without materializing JsonValue
  fn deserialize<T: Deserialize>(JsonSlice): Result<own<T>, JsonError>;
}
```

### 2.3 Typical usage pattern

```typescript
function handle_request(body: ref<str>): Result<own<Response>, AppError> {
  const doc = JsonSlice::parse(body)?;

  const user_id  = doc.get("user_id").and_then(|s| s.as_u64())
                      .ok_or(AppError::missing_field("user_id"))?;
  const action   = doc.get("action").and_then(|s| s.as_str())
                      .ok_or(AppError::missing_field("action"))?;
  const payload  = doc.get("payload");

  // user_id, action, payload all borrow `body` — no heap allocation
  // doc, user_id, action, payload all dropped at end of scope
  dispatch(user_id, action, payload)
}
```

---

## 3. `derive(Serialize, Deserialize)` — Typed Layer

The primary API. Annotate your struct; the compiler generates an optimised parser
directly into `own<T>` and a serializer that writes directly to a `JsonWriter` — no
intermediate `JsonValue` tree.

### 3.1 Basic usage

```typescript
@derive(Serialize, Deserialize)
struct User {
  id:    u64,
  name:  own<String>,
  email: own<String>,
  age:   Option<u32>,              // missing or null → None
  roles: own<Vec<String>>,
}

// Deserialize
const user = User::from_json(input)?;         // Result<own<User>, JsonError>

// Serialize
const json  = user.to_json();                 // own<String>
user.write_json(refmut(stdout_writer))?;      // streaming, no intermediate String
```

### 3.2 Field annotations

```typescript
@derive(Serialize, Deserialize)
struct Config {
  timeout:   u32,

  // Rename key in JSON
  @rename("max_connections")
  maxConns:  u32,

  // Field is optional — missing → provided default expression
  @default(30)
  retries:   u32,

  // Omit from serialized output when None
  @skip_serializing_if(Option::is_none)
  tag:       Option<own<String>>,

  // Skip this field entirely — never serialized or deserialized
  @skip
  internal:  u32,

  // Flatten a nested struct's fields into this object's keys
  @flatten
  metadata:  Metadata,

  // Collect all unknown fields into this map
  // Type must be HashMap<String, JsonValue>
  @extra
  unknown:   own<HashMap<String, JsonValue>>,
}
```

**`@extra` field rules:**
- Type must be exactly `own<HashMap<String, JsonValue>>`
- Maximum one `@extra` field per struct
- Unknown fields are collected here; known fields are excluded
- On serialization, `@extra` entries are merged into the output object
- If no `@extra` field is present, unknown fields are silently ignored (forward-compat)
- To error on unknown fields: `@deny_unknown_fields` on the struct (see §3.4)

### 3.3 Enum serialization

```typescript
@derive(Serialize, Deserialize)
enum Command {
  Quit,
  Move { x: i32, y: i32 },
  Write(own<String>),
  ChangeColor(i32, i32, i32),
}
```

Default representation: **externally tagged** (matches serde default):

```json
"Quit"                              // unit variant
{"Move": {"x": 1, "y": 2}}         // struct variant
{"Write": "hello"}                  // newtype variant
{"ChangeColor": [255, 0, 128]}      // tuple variant
```

Alternative representations via struct annotation:

```typescript
// Internally tagged: { "type": "Move", "x": 1, "y": 2 }
@tag("type")
@derive(Serialize, Deserialize)
enum Command { ... }

// Adjacently tagged: { "type": "Move", "content": {"x": 1, "y": 2} }
@tag("type") @content("content")
@derive(Serialize, Deserialize)
enum Command { ... }

// Untagged: best-effort structural match (slower, may be ambiguous)
@untagged
@derive(Serialize, Deserialize)
enum Command { ... }
```

### 3.4 Struct-level annotations

```typescript
// Rename all fields: snake_case (default), camelCase, PascalCase, SCREAMING_SNAKE_CASE
@rename_all("camelCase")
@derive(Serialize, Deserialize)
struct ApiResponse { status_code: u32, response_body: own<String> }
// serializes as: { "statusCode": ..., "responseBody": ... }

// Error on any unknown field (strict mode — overrides default ignore behaviour)
@deny_unknown_fields
@derive(Serialize, Deserialize)
struct StrictConfig { timeout: u32 }

// Do not emit null fields in serialized output (emit only Some(v) fields)
@skip_none
@derive(Serialize, Deserialize)
struct SparseConfig { a: Option<u32>, b: Option<u32> }
```

### 3.5 The `Serialize` and `Deserialize` traits

```typescript
trait Serialize {
  fn serialize<W: Write>(ref<Self>, w: refmut<JsonWriter<W>>)
    : Result<void, JsonError>;

  // Provided:
  fn to_json(ref<Self>): own<String>;
  fn write_json<W: Write>(ref<Self>, sink: refmut<W>): Result<void, JsonError>;
  fn to_json_pretty(ref<Self>, indent: usize): own<String>;
}

trait Deserialize: Sized {
  fn from_json(input: ref<str>): Result<own<Self>, JsonError>;
  fn from_slice(slice: JsonSlice): Result<own<Self>, JsonError>;
  fn from_value(val: own<JsonValue>): Result<own<Self>, JsonError>;
}
```

Standard library types that implement `Serialize` + `Deserialize`:

| Type | JSON representation |
|------|---------------------|
| `bool` | `true` / `false` |
| `i8`–`i64`, `isize` | JSON integer |
| `u8`–`u64`, `usize` | JSON integer |
| `f32`, `f64` | JSON number (finite only; NaN/Inf → error) |
| `String` | JSON string |
| `Option<T>` | `null` (None) or `T` (Some) |
| `Vec<T>` | JSON array |
| `StaticArray<T,N>` | JSON array (fixed length; error if wrong length) |
| `HashMap<String, V>` | JSON object |
| `BTreeMap<String, V>` | JSON object (keys sorted) |
| `(T, U)` | `[t, u]` (two-element array) |
| `JsonValue` | any JSON value (passthrough) |

---

## 4. `JsonWriter` — Streaming Serializer

Used by `derive(Serialize)` internally, and available directly for custom serialization.

```typescript
class JsonWriter<W: Write> {
  static new(sink: refmut<W>): own<JsonWriter<W>>;
  static pretty(sink: refmut<W>, indent: usize): own<JsonWriter<W>>;

  // Primitives
  fn write_null(refmut<Self>): Result<void, JsonError>;
  fn write_bool(refmut<Self>, v: bool): Result<void, JsonError>;
  fn write_i64(refmut<Self>, v: i64): Result<void, JsonError>;
  fn write_u64(refmut<Self>, v: u64): Result<void, JsonError>;
  fn write_f64(refmut<Self>, v: f64): Result<void, JsonError>;
  fn write_str(refmut<Self>, v: ref<str>): Result<void, JsonError>;  // handles escaping

  // Structural
  fn begin_object(refmut<Self>): Result<void, JsonError>;
  fn write_key(refmut<Self>, key: ref<str>): Result<void, JsonError>;
  fn end_object(refmut<Self>): Result<void, JsonError>;
  fn begin_array(refmut<Self>): Result<void, JsonError>;
  fn end_array(refmut<Self>): Result<void, JsonError>;

  // Convenience — write any Serialize value
  fn write_value<T: Serialize>(refmut<Self>, v: ref<T>): Result<void, JsonError>;
}
```

**State machine:** `JsonWriter` tracks nesting depth and automatically inserts commas and
colons. Writing in an invalid sequence (e.g. two consecutive `write_key` calls, or calling
`end_array` inside an object) is a runtime error in debug and undefined behaviour in
release. The validity checks are elided in release builds for performance.

### Streaming large responses

```typescript
function write_users_response<W: Write>(
  users: ref<[User]>,
  writer: refmut<W>
): Result<void, IoError> {
  const jw = JsonWriter::new(writer);
  jw.begin_array()?;
  for (const user of users.iter()) {
    jw.write_value(user)?;          // streams directly, no intermediate String
  }
  jw.end_array()?;
  Ok(())
}
```

---

## 5. `JsonError` — Error Type

```typescript
enum JsonError {
  // Parse errors
  UnexpectedChar { pos: usize, got: char, expected: ref<str> },
  UnexpectedEnd  { pos: usize },
  InvalidEscape  { pos: usize },
  InvalidUtf8    { pos: usize },
  NumberOutOfRange { pos: usize, raw: own<String> },
  MaxDepthExceeded { depth: usize },

  // Deserialize errors
  MissingField   { field: own<String> },
  WrongType      { field: own<String>, expected: own<String>, got: own<String> },
  UnknownVariant { variant: own<String> },
  DuplicateField { field: own<String> },      // emitted only in @deny_unknown_fields mode
  UnknownField   { field: own<String> },      // emitted only in @deny_unknown_fields mode

  // Serialize errors
  NanOrInfinite,            // f64 NaN or Inf — not representable in JSON
  WriteError(IoError),

  // Custom — for user-defined Deserialize impls
  Custom(own<String>),
}

impl Error for JsonError { ... }
impl Display for JsonError { ... }
```

**Error positions** are byte offsets into the input string. The `JsonError` display
implementation converts to `line:column` format by scanning the input up to the offset.

---

## 6. Parser Implementation Notes

### 6.1 Stack depth limit

The recursive descent parser limits nesting depth to **128 levels** by default. Beyond
this limit, `JsonError::MaxDepthExceeded` is returned. Configurable via:

```typescript
JsonParser::new().max_depth(256).parse(input)?;
```

`JsonParser` is the low-level struct used by all three layers internally:

```typescript
class JsonParser {
  static new(): own<JsonParser>;
  fn max_depth(own<JsonParser>, n: usize): own<JsonParser>;

  fn parse_value(refmut<JsonParser>, input: ref<str>)
    : Result<own<JsonValue>, JsonError>;
  fn parse_slice(refmut<JsonParser>, input: ref<str>)
    : Result<JsonSlice, JsonError>;
}
```

### 6.2 String handling — escape sequences

Strings in the zero-copy layer (`JsonSlice`) return `as_str() → Option<ref<str>>` only
when the raw JSON string contains no backslash characters (most real-world JSON). If a
backslash is detected, `as_str()` returns `None` and `as_str_owned()` must be used, which
allocates and resolves escapes.

Escape sequences handled: `\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`, `\uXXXX`,
`\uXXXX\uXXXX` (surrogate pairs → single Unicode scalar). Invalid surrogates → error.

### 6.3 Number parsing

Numbers are parsed left-to-right in one pass:
1. Leading `-` → signed candidate
2. Integer digits only → try `i64` (signed) or `u64` (unsigned, positive only)
3. Decimal point or exponent → `f64`
4. Overflow at any step → `JsonError::NumberOutOfRange`

`f64` parsing uses the Grisu3/Dragon4 algorithm for round-trip correctness.

### 6.4 RFC 8259 strict compliance

The following are **parse errors**, not silently accepted:

- Trailing commas in arrays or objects
- Comments (`//`, `/* */`)
- Single-quoted strings
- Unquoted keys
- `NaN`, `Infinity`, `-Infinity` literals
- Leading zeros in integers (`01`)
- Bare values at top level — wait, RFC 8259 §2 allows any value at top level, so
  `"hello"`, `42`, `true`, `null` are all valid top-level documents

---

## 7. Module Layout

```
std::json
├── JsonValue          — owned dynamic tree
├── JsonSlice          — zero-copy borrowed view
├── JsonWriter         — streaming serializer
├── JsonParser         — low-level parser (rarely used directly)
├── JsonError          — unified error type
├── Serialize          — trait (also in std::json::ser)
├── Deserialize        — trait (also in std::json::de)
└── prelude            — re-exports: JsonValue, JsonSlice, Serialize, Deserialize, JsonError
```

Import pattern:

```typescript
// Most users:
import { Serialize, Deserialize } from 'std/json';

// Dynamic JSON:
import { JsonValue } from 'std/json';

// Zero-copy:
import { JsonSlice } from 'std/json';

// Everything:
import * from 'std/json/prelude';
```

---

## 8. Design Decisions Recorded

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Number variants | Separate `Int(i64)`, `Uint(u64)`, `Float(f64)` | No precision loss; `u64::MAX` representable |
| Unknown fields | Silently ignored by default; `@extra` collects; `@deny_unknown_fields` errors | Forward-compatibility is the safe default for most APIs |
| `JsonSlice` location | `std::json` core (not separate crate) | Wasm perf requires zero-copy in the primary library |
| Extensions | Strict RFC 8259 only | Simpler parser, auditable, no ambiguity |
| Object ordering | Preserve insertion order (`Vec` of pairs, not `HashMap`) | Required by some APIs; `into_map()` available for lookup-heavy use |
| `f64` NaN/Inf | Serialize error | Not representable in RFC 8259 |
| Default unknown field behaviour | Ignore (not error) | Forward-compatibility |
| Nesting limit | 128 levels default, configurable | Prevents stack overflow on malicious input |
