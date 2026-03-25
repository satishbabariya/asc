# RFC-0019 — Std: Path and Config Formats

| Field       | Value                                           |
|-------------|-------------------------------------------------|
| Status      | Accepted                                        |
| Depends on  | RFC-0011, RFC-0013, RFC-0014                    |
| Module path | `std::path`, `std::env`, `std::config`          |
| Inspired by | Deno `@std/path`, `@std/dotenv`, `@std/toml`, `@std/yaml`, `@std/ini` |

## Summary

Three modules:

- **`std::path`** — string-based path manipulation, cross-platform (POSIX on Wasm/Linux/macOS,
  Windows paths on Windows target). Complements the `std::fs::Path` owned type from
  RFC-0014 with pure string manipulation functions that require no filesystem access.
- **`std::env`** — environment variable access and `.env` file loading.
- **`std::config`** — structured config format parsing and serialization: TOML, YAML, INI.
  All formats support `derive(Deserialize)` from RFC-0016's trait infrastructure.

---

## 1. `std::path` — Path string manipulation

`std::path` operates on `ref<str>` slices. It never allocates unless it has to join or
normalize paths. All functions work on POSIX paths by default; the Windows variant is
gated behind the `windows` target.

### 1.1 Inspection

```typescript
// Extract the final component of a path.
// "/foo/bar/baz.ts" → "baz.ts"
// "/foo/bar/" → ""  (trailing slash means no filename)
function basename(path: ref<str>): ref<str>;

// Remove the final component.
// "/foo/bar/baz.ts" → "/foo/bar"
// "/foo/bar/" → "/foo/bar"  (trailing slash treated as part of dirname)
function dirname(path: ref<str>): ref<str>;

// Extract the file extension (without the dot).
// "baz.ts" → "ts"
// "baz.tar.gz" → "gz"  (only the last extension)
// ".hidden" → ""  (dot-files have no extension)
function extname(path: ref<str>): ref<str>;

// Extract basename without extension.
// "baz.ts" → "baz"
// ".hidden" → ".hidden"
function stem(path: ref<str>): ref<str>;

// Is the path absolute?
function is_absolute(path: ref<str>): bool;

// Is the path relative?
function is_relative(path: ref<str>): bool;

// Does the path have a trailing separator?
function has_trailing_sep(path: ref<str>): bool;
```

### 1.2 Construction

```typescript
// Join path components. Handles absolute components correctly:
// if any component is absolute, all preceding components are discarded.
// join("/foo", "bar", "baz") → "/foo/bar/baz"
// join("/foo", "/bar") → "/bar"  (second component is absolute)
function join(parts: ref<[ref<str>]>): own<String>;

// Variadic-style join (up to 8 components via overloading)
function join2(a: ref<str>, b: ref<str>): own<String>;
function join3(a: ref<str>, b: ref<str>, c: ref<str>): own<String>;
// ... join4 through join8

// Resolve a sequence of paths into a single absolute path.
// Equivalent to applying each path as a cd from the last.
// resolve("/foo", "bar", "../baz") → "/foo/baz"
// If no absolute path is in the list, resolves relative to cwd.
function resolve(parts: ref<[ref<str>]>): Result<own<String>, PathError>;

// Make path2 relative to path1.
// relative("/foo/bar", "/foo/baz") → "../baz"
function relative(from: ref<str>, to: ref<str>): own<String>;

// Normalize a path: resolve . and .. without filesystem access.
// normalize("/foo/../bar/./baz") → "/bar/baz"
function normalize(path: ref<str>): own<String>;

// Add or replace file extension.
// with_extname("foo.ts", "js") → "foo.js"
// with_extname("foo", "js") → "foo.js"
function with_extname(path: ref<str>, ext: ref<str>): own<String>;

// Replace the basename.
// with_basename("/foo/bar.ts", "baz.ts") → "/foo/baz.ts"
function with_basename(path: ref<str>, name: ref<str>): own<String>;
```

### 1.3 Glob matching

```typescript
// Test if a path matches a glob pattern.
// Supports: * (any chars except sep), ** (any chars including sep), ? (single char),
//           [abc] (char class), {a,b} (alternation)
function glob_match(path: ref<str>, pattern: ref<str>): bool;

// Expand a glob pattern against the filesystem. Returns matching paths.
// Requires fs permission.
function glob(pattern: ref<str>): Result<own<Vec<String>>, IoError>;
```

### 1.4 Platform separator

```typescript
// Platform separator: '/' on POSIX, '\\' on Windows.
const SEP: char;          // '/' on Wasm/POSIX
const SEP_STR: ref<str>;  // "/" on Wasm/POSIX

// Convert all separators to the platform separator.
function to_platform_sep(path: ref<str>): own<String>;
// Convert all separators to forward slash.
function to_posix_sep(path: ref<str>): own<String>;
```

---

## 2. `std::env` — Environment and `.env` files

### 2.1 Environment variables

```typescript
// Get an environment variable. Returns None if not set.
// On Wasm: reads from WASI environ (requires env permission).
function var(name: ref<str>): Option<own<String>>;

// Get environment variable or return a default.
function var_or(name: ref<str>, default: own<String>): own<String>;

// Get all environment variables as a HashMap.
function vars(): own<HashMap<String, String>>;

// Set an environment variable (current process only).
// On Wasm: updates the WASI environ.
function set_var(name: ref<str>, value: ref<str>): void;

// Remove an environment variable.
function remove_var(name: ref<str>): void;

// Current working directory.
function cwd(): Result<own<String>, IoError>;

// Current executable path.
function exe(): Result<own<String>, IoError>;
```

### 2.2 `.env` file loading

```typescript
// Parse and load a .env file into the current environment.
// Ignores lines starting with #. Strips surrounding quotes.
// Does NOT override existing env vars by default.
function load_dotenv(path: ref<str>): Result<void, EnvError>;

// Load and override existing vars.
function load_dotenv_override(path: ref<str>): Result<void, EnvError>;

// Parse a .env file into a HashMap without modifying the environment.
function parse_dotenv(content: ref<str>): Result<own<HashMap<String, String>>, EnvError>;

// Load the default .env file (searches .env, .env.local in cwd and parents).
function load_default_dotenv(): Result<void, EnvError>;
```

`.env` file format supported:

```
# Comment
KEY=value
KEY="quoted value with spaces"
KEY='single quoted'
KEY=value with # inline comment stripped
MULTI_LINE="line1\nline2"   # \n escape in double-quoted values
export KEY=value             # export keyword ignored
```

### 2.3 Typed env access

```typescript
// Parse an env var into a typed value. T must implement FromStr.
// Returns Err if not set or if parsing fails.
function var_parsed<T: FromStr>(name: ref<str>): Result<own<T>, EnvError>;

// enum EnvError
enum EnvError {
  NotSet { name: own<String> },
  ParseError { name: own<String>, value: own<String>, expected_type: own<String> },
  InvalidFormat { line: usize, content: own<String> },
  IoError(IoError),
}
```

---

## 3. `std::config` — Config format parsing

All parsers in this module produce either a typed `own<T>` (via `derive(Deserialize)`)
or a `JsonValue`-equivalent dynamic tree for the respective format.

### 3.1 TOML

TOML v1.0 compliant. The dynamic tree uses the same `JsonValue` infrastructure from
RFC-0016 extended with a `Datetime` variant (TOML has native date/time types).

```typescript
namespace toml {
  // Parse TOML into a typed struct. Uses the same Deserialize trait as JSON.
  function from_str<T: Deserialize>(input: ref<str>): Result<own<T>, TomlError>;

  // Parse TOML into a dynamic value tree.
  function parse(input: ref<str>): Result<own<TomlValue>, TomlError>;

  // Serialize a typed value to TOML string.
  function to_string<T: Serialize>(value: ref<T>): Result<own<String>, TomlError>;

  // Serialize with formatting options.
  function to_string_pretty<T: Serialize>(value: ref<T>): Result<own<String>, TomlError>;

  // TOML value tree (extends JSON with date/time)
  enum TomlValue {
    Str(own<String>),
    Int(i64),
    Float(f64),
    Bool(bool),
    Datetime(own<String>),   // RFC 3339 datetime string
    Array(own<Vec<TomlValue>>),
    Table(own<Vec<(own<String>, TomlValue)>>),  // ordered
  }

  enum TomlError {
    Parse { line: usize, col: usize, message: own<String> },
    Serialize { message: own<String> },
    Deserialize { field: own<String>, message: own<String> },
  }
}
```

TOML `derive(Deserialize)` example:

```typescript
@derive(Serialize, Deserialize)
struct ServerConfig {
  host: own<String>,
  port: u16,
  workers: Option<u32>,
  @rename("tls")
  tls_enabled: bool,
}

const config = toml::from_str<ServerConfig>(toml_text)?;
```

### 3.2 YAML

YAML 1.2 core schema (the safe subset — no !! type tags, no anchors in the API,
no arbitrary code execution).

```typescript
namespace yaml {
  // Parse YAML into a typed struct.
  function from_str<T: Deserialize>(input: ref<str>): Result<own<T>, YamlError>;

  // Parse YAML into a dynamic value tree.
  function parse(input: ref<str>): Result<own<YamlValue>, YamlError>;

  // Parse multi-document YAML (--- separators).
  function parse_all(input: ref<str>): Result<own<Vec<YamlValue>>, YamlError>;

  // Serialize.
  function to_string<T: Serialize>(value: ref<T>): Result<own<String>, YamlError>;

  enum YamlValue {
    Null,
    Bool(bool),
    Int(i64),
    Uint(u64),
    Float(f64),
    Str(own<String>),
    Sequence(own<Vec<YamlValue>>),
    Mapping(own<Vec<(own<YamlValue>, YamlValue)>>),  // YAML allows non-string keys
  }

  enum YamlError {
    Parse { line: usize, col: usize, message: own<String> },
    UnsafeType { tag: own<String> },   // !! type tags rejected
    Serialize { message: own<String> },
  }
}
```

### 3.3 INI

Simple key-value config format with optional sections. No standard — we follow the
most common convention (Windows INI / Python configparser style).

```typescript
namespace ini {
  // Parse INI into a HashMap<section, HashMap<key, value>>.
  // Keys without a section go into the "" (empty string) section.
  function parse(input: ref<str>): Result<own<HashMap<String, HashMap<String, String>>>, IniError>;

  // Parse into a typed struct. Top-level fields = global section keys.
  // Nested structs = sections.
  function from_str<T: Deserialize>(input: ref<str>): Result<own<T>, IniError>;

  // Serialize.
  function to_string<T: Serialize>(value: ref<T>): Result<own<String>, IniError>;

  @copy
  struct ParseOptions {
    comment_chars: ref<str>,       // default: "#;"
    assignment_chars: ref<str>,    // default: "=:"
    allow_no_value: bool,          // keys without = → value is empty string
    multiline_values: bool,        // continuation lines starting with whitespace
    interpolation: bool,           // %(key)s interpolation
  }

  const DEFAULT_OPTIONS: ParseOptions = ParseOptions {
    comment_chars: "#;",
    assignment_chars: "=:",
    allow_no_value: false,
    multiline_values: false,
    interpolation: false,
  };

  function parse_with(input: ref<str>, opts: ParseOptions)
    : Result<own<HashMap<String, HashMap<String, String>>>, IniError>;

  enum IniError {
    InvalidSection { line: usize },
    InvalidKey { line: usize },
    InterpolationError { key: own<String>, reference: own<String> },
  }
}
```

---

## 4. Module layout

```
std::path
├── basename, dirname, extname, stem
├── is_absolute, is_relative, has_trailing_sep
├── join, join2..join8, resolve, relative, normalize
├── with_extname, with_basename
├── glob_match, glob
└── SEP, SEP_STR, to_platform_sep, to_posix_sep

std::env
├── var, var_or, vars, set_var, remove_var
├── cwd, exe
├── load_dotenv, load_dotenv_override, parse_dotenv, load_default_dotenv
├── var_parsed
└── EnvError

std::config
├── toml::{from_str, parse, to_string, to_string_pretty, TomlValue, TomlError}
├── yaml::{from_str, parse, parse_all, to_string, YamlValue, YamlError}
└── ini::{parse, from_str, to_string, parse_with, ParseOptions, IniError}
```

Import pattern:

```typescript
import { join, basename, extname } from 'std/path';
import { load_dotenv, var_parsed } from 'std/env';
import { toml, yaml } from 'std/config';
```
