# Correctness Fixes + RFC-0011/0013 Depth Push — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix Sema trait signature mismatches, replace BTreeMap/BTreeSet stubs with real implementations, register missing traits, add missing Iterator/Vec/String/HashMap methods to push RFC-0011 to ~92% and RFC-0013 to ~88%.

**Architecture:** Three phases in dependency order: (1) correctness fixes to Builtins.cpp + BTree stubs + commit untracked files, (2) RFC-0011 trait registrations + iterator combinators + Display fixes, (3) RFC-0013 Vec/String/HashMap method additions. Compiler rebuilds after each Builtins.cpp change. All std library work is pure .ts — no codegen changes.

**Tech Stack:** C++ (Builtins.cpp), ASC TypeScript (std library), lit test framework, cmake build system.

**Test format:** Every `.ts` test file starts with `// RUN: %asc check %s` and has a `function main(): i32 { ... return 0; }` entry point. Uses `assert!()` and `assert_eq!()` macros. Run all tests: `lit test/ --no-progress-bar`.

---

## Phase 1: Correctness Fixes

### Task 1: Fix Display Trait Signature in Sema

**Files:**
- Modify: `lib/Sema/Builtins.cpp:378-403`

- [ ] **Step 1: Fix the Display trait registration**

In `lib/Sema/Builtins.cpp`, replace the Display trait block (lines 378-403) with:

```cpp
  // Display trait: fn fmt(ref<Self>, refmut<Formatter>): Result<void, FmtError>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl fmtParam;
    fmtParam.name = "f";
    fmtParam.type = ctx.create<RefMutType>(
        ctx.create<NamedType>("Formatter", std::vector<Type *>{}, loc), loc);
    fmtParam.loc = loc;
    auto *fmtMethod = ctx.create<FunctionDecl>(
        "fmt", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, fmtParam},
        ctx.create<NamedType>("Result", std::vector<Type *>{
            ctx.getVoidType(),
            ctx.create<NamedType>("FmtError", std::vector<Type *>{}, loc)
        }, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem fmtItem;
    fmtItem.method = fmtMethod;
    auto *displayTrait = ctx.create<TraitDecl>(
        "Display", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{fmtItem}, loc);
    traitDecls["Display"] = displayTrait;
    Symbol sym;
    sym.name = "Display";
    sym.decl = displayTrait;
    scope->declare("Display", std::move(sym));
  }
```

- [ ] **Step 2: Fix the Debug trait to also use fmt method**

Replace the Debug trait block (lines 405-416) with:

```cpp
  // Debug trait: fn fmt(ref<Self>, refmut<Formatter>): Result<void, FmtError>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl fmtParam;
    fmtParam.name = "f";
    fmtParam.type = ctx.create<RefMutType>(
        ctx.create<NamedType>("Formatter", std::vector<Type *>{}, loc), loc);
    fmtParam.loc = loc;
    auto *fmtMethod = ctx.create<FunctionDecl>(
        "fmt", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, fmtParam},
        ctx.create<NamedType>("Result", std::vector<Type *>{
            ctx.getVoidType(),
            ctx.create<NamedType>("FmtError", std::vector<Type *>{}, loc)
        }, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem fmtItem;
    fmtItem.method = fmtMethod;
    auto *debugTrait = ctx.create<TraitDecl>(
        "Debug", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{fmtItem}, loc);
    traitDecls["Debug"] = debugTrait;
    Symbol sym;
    sym.name = "Debug";
    sym.decl = debugTrait;
    scope->declare("Debug", std::move(sym));
  }
```

### Task 2: Fix PartialOrd/Ord Trait Signatures in Sema

**Files:**
- Modify: `lib/Sema/Builtins.cpp:662-723`

- [ ] **Step 1: Fix PartialOrd return type from i32 to Option\<Ordering\>**

Replace the PartialOrd block (lines 662-691) with:

```cpp
  // PartialOrd trait: fn partial_cmp(ref<Self>, ref<Self>): Option<Ordering>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl otherParam;
    otherParam.name = "other";
    otherParam.type = ctx.create<RefType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    otherParam.loc = loc;
    auto *partialCmpMethod = ctx.create<FunctionDecl>(
        "partial_cmp", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, otherParam},
        ctx.create<NamedType>("Option", std::vector<Type *>{
            ctx.create<NamedType>("Ordering", std::vector<Type *>{}, loc)
        }, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem partialCmpItem;
    partialCmpItem.method = partialCmpMethod;
    auto *partialOrdTrait = ctx.create<TraitDecl>(
        "PartialOrd", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{partialCmpItem}, loc);
    traitDecls["PartialOrd"] = partialOrdTrait;
    Symbol sym;
    sym.name = "PartialOrd";
    sym.decl = partialOrdTrait;
    scope->declare("PartialOrd", std::move(sym));
  }
```

- [ ] **Step 2: Fix Ord return type from i32 to Ordering**

Replace the Ord block (lines 694-723) with:

```cpp
  // Ord trait: fn cmp(ref<Self>, ref<Self>): Ordering
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl otherParam;
    otherParam.name = "other";
    otherParam.type = ctx.create<RefType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    otherParam.loc = loc;
    auto *cmpMethod = ctx.create<FunctionDecl>(
        "cmp", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, otherParam},
        ctx.create<NamedType>("Ordering", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem cmpItem;
    cmpItem.method = cmpMethod;
    auto *ordTrait = ctx.create<TraitDecl>(
        "Ord", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{cmpItem}, loc);
    traitDecls["Ord"] = ordTrait;
    Symbol sym;
    sym.name = "Ord";
    sym.decl = ordTrait;
    scope->declare("Ord", std::move(sym));
  }
```

### Task 3: Fix Hash Trait Signature in Sema

**Files:**
- Modify: `lib/Sema/Builtins.cpp:726-751`

- [ ] **Step 1: Fix Hash to take refmut\<Hasher\> and return void**

Replace the Hash block (lines 726-751) with:

```cpp
  // Hash trait: fn hash(ref<Self>, refmut<Hasher>): void
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl hasherParam;
    hasherParam.name = "hasher";
    hasherParam.type = ctx.create<RefMutType>(
        ctx.create<NamedType>("Hasher", std::vector<Type *>{}, loc), loc);
    hasherParam.loc = loc;
    auto *hashMethod = ctx.create<FunctionDecl>(
        "hash", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, hasherParam},
        ctx.getVoidType(), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem hashItem;
    hashItem.method = hashMethod;
    auto *hashTrait = ctx.create<TraitDecl>(
        "Hash", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{hashItem}, loc);
    traitDecls["Hash"] = hashTrait;
    Symbol sym;
    sym.name = "Hash";
    sym.decl = hashTrait;
    scope->declare("Hash", std::move(sym));
  }
```

### Task 4: Rebuild Compiler and Validate

- [ ] **Step 1: Rebuild the compiler**

Run: `cmake --build build/ -j$(sysctl -n hw.ncpu)`

Expected: Build succeeds with 0 errors.

- [ ] **Step 2: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: 237 tests, 100% pass. If any test regresses, investigate — the trait signature changes should be transparent since no test dispatches on these method names.

- [ ] **Step 3: Commit**

```bash
git add lib/Sema/Builtins.cpp
git commit -m "fix: correct Sema trait signatures for Display/PartialOrd/Ord/Hash (RFC-0011)"
```

### Task 5: Replace BTreeMap Stub with Real B-Tree

**Files:**
- Modify: `std/collections/btreemap.ts` (full rewrite)

- [ ] **Step 1: Write the BTreeMap test**

Create `test/std/test_btreemap.ts`:

```typescript
// RUN: %asc check %s
// Test: BTreeMap<K,V> with sorted order, remove, first/last.
function main(): i32 {
  let map: BTreeMap<i32, i32> = BTreeMap::new();
  assert!(map.is_empty());

  // Insert out of order — must maintain sorted order internally.
  map.insert(30, 300);
  map.insert(10, 100);
  map.insert(20, 200);
  assert_eq!(map.len(), 3);

  // Get existing key.
  assert_eq!(map.get(10).unwrap(), 100);
  assert_eq!(map.get(20).unwrap(), 200);
  assert_eq!(map.get(30).unwrap(), 300);

  // Get non-existing key.
  assert!(map.get(15).is_none());

  // Contains.
  assert!(map.contains_key(10));
  assert!(!map.contains_key(99));

  // First / last key-value.
  const first = map.first_key_value().unwrap();
  assert_eq!(first.0, 10);
  assert_eq!(first.1, 100);

  const last = map.last_key_value().unwrap();
  assert_eq!(last.0, 30);
  assert_eq!(last.1, 300);

  // Duplicate key — update value.
  const old = map.insert(20, 999);
  assert_eq!(old.unwrap(), 200);
  assert_eq!(map.len(), 3);
  assert_eq!(map.get(20).unwrap(), 999);

  // Remove.
  const removed = map.remove(20);
  assert_eq!(removed.unwrap(), 999);
  assert_eq!(map.len(), 2);
  assert!(map.get(20).is_none());

  // Remove non-existing.
  assert!(map.remove(20).is_none());

  // Clear.
  map.clear();
  assert!(map.is_empty());

  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/std/test_btreemap.ts -v`

Expected: FAIL (current insert doesn't maintain sorted order, remove doesn't exist)

- [ ] **Step 3: Rewrite BTreeMap implementation**

Replace `std/collections/btreemap.ts` entirely:

```typescript
// std/collections/btreemap.ts — BTreeMap<K,V> (RFC-0013)
// B-tree with order B=6 (max 11 keys per node).

import { Ord, Ordering } from '../core/cmp';

const B: usize = 6;
const MAX_KEYS: usize = 2 * B - 1;
const MIN_KEYS: usize = B - 1;

struct BTreeNode<K, V> {
  keys: own<Vec<K>>,
  values: own<Vec<V>>,
  children: own<Vec<own<BTreeNode<K, V>>>>,
  is_leaf: bool,
}

struct BTreeMap<K: Ord, V> {
  root: Option<own<BTreeNode<K, V>>>,
  len: usize,
}

impl<K: Ord, V> BTreeMap<K, V> {
  fn new(): own<BTreeMap<K, V>> {
    return BTreeMap { root: Option::None, len: 0 };
  }

  fn len(ref<Self>): usize { return self.len; }
  fn is_empty(ref<Self>): bool { return self.len == 0; }

  fn get(ref<Self>, key: ref<K>): Option<ref<V>> {
    match self.root {
      Option::Some(ref node) => { return BTreeMap::search_node(node, key); },
      Option::None => { return Option::None; },
    }
  }

  fn search_node(node: ref<BTreeNode<K, V>>, key: ref<K>): Option<ref<V>> {
    let i: usize = 0;
    while i < node.keys.len() {
      match key.cmp(node.keys.get(i).unwrap()) {
        Ordering::Equal => { return node.values.get(i); },
        Ordering::Less => {
          if node.is_leaf { return Option::None; }
          return BTreeMap::search_node(node.children.get(i).unwrap(), key);
        },
        Ordering::Greater => { i = i + 1; },
      }
    }
    if node.is_leaf { return Option::None; }
    return BTreeMap::search_node(node.children.get(i).unwrap(), key);
  }

  fn contains_key(ref<Self>, key: ref<K>): bool {
    return self.get(key).is_some();
  }

  fn insert(refmut<Self>, key: own<K>, value: own<V>): Option<own<V>> {
    match self.root {
      Option::None => {
        let node = BTreeNode {
          keys: Vec::new(),
          values: Vec::new(),
          children: Vec::new(),
          is_leaf: true,
        };
        node.keys.push(key);
        node.values.push(value);
        self.root = Option::Some(node);
        self.len = self.len + 1;
        return Option::None;
      },
      Option::Some(ref root) => {
        // Check if key exists first — update in place.
        let found_idx = BTreeMap::find_key_in_node(root, &key);
        if found_idx >= 0 {
          // Key exists — swap value. Walk to the node containing it.
          const old = BTreeMap::replace_value(root, &key, value);
          return old;
        }

        // Insert into the tree. If root is full, split it first.
        if root.keys.len() == MAX_KEYS {
          let new_root = BTreeNode {
            keys: Vec::new(),
            values: Vec::new(),
            children: Vec::new(),
            is_leaf: false,
          };
          // Move old root to be child of new root.
          let old_root = self.root.take().unwrap();
          new_root.children.push(old_root);
          BTreeMap::split_child(&new_root, 0);
          BTreeMap::insert_non_full(&new_root, key, value);
          self.root = Option::Some(new_root);
        } else {
          BTreeMap::insert_non_full(root, key, value);
        }
        self.len = self.len + 1;
        return Option::None;
      },
    }
  }

  fn find_key_in_node(node: ref<BTreeNode<K, V>>, key: ref<K>): i32 {
    let i: usize = 0;
    while i < node.keys.len() {
      match key.cmp(node.keys.get(i).unwrap()) {
        Ordering::Equal => { return i as i32; },
        Ordering::Less => {
          if node.is_leaf { return -1; }
          return BTreeMap::find_key_in_node(node.children.get(i).unwrap(), key);
        },
        Ordering::Greater => { i = i + 1; },
      }
    }
    if node.is_leaf { return -1; }
    return BTreeMap::find_key_in_node(node.children.get(i).unwrap(), key);
  }

  fn replace_value(node: refmut<BTreeNode<K, V>>, key: ref<K>, value: own<V>): Option<own<V>> {
    let i: usize = 0;
    while i < node.keys.len() {
      match key.cmp(node.keys.get(i).unwrap()) {
        Ordering::Equal => {
          // Swap value at index i.
          const elem_size = size_of!<V>();
          const slot = (node.values.ptr as usize + i * elem_size) as *mut V;
          const old = unsafe { ptr_read(slot) };
          unsafe { ptr_write(slot, value); }
          return Option::Some(old);
        },
        Ordering::Less => {
          if !node.is_leaf {
            return BTreeMap::replace_value(node.children.get_mut(i).unwrap(), key, value);
          }
          return Option::None;
        },
        Ordering::Greater => { i = i + 1; },
      }
    }
    if !node.is_leaf {
      return BTreeMap::replace_value(node.children.get_mut(i).unwrap(), key, value);
    }
    return Option::None;
  }

  fn insert_non_full(node: refmut<BTreeNode<K, V>>, key: own<K>, value: own<V>): void {
    if node.is_leaf {
      // Find insertion position (sorted).
      let i: usize = node.keys.len();
      // Walk backwards to find correct position.
      // We insert at position i where key > keys[i-1].
      while i > 0 {
        match key.cmp(node.keys.get(i - 1).unwrap()) {
          Ordering::Greater => { break; },
          _ => { i = i - 1; },
        }
      }
      node.keys.insert(i, key);
      node.values.insert(i, value);
    } else {
      // Find child to descend into.
      let i: usize = node.keys.len();
      while i > 0 {
        match key.cmp(node.keys.get(i - 1).unwrap()) {
          Ordering::Greater => { break; },
          _ => { i = i - 1; },
        }
      }
      // If child is full, split it.
      if node.children.get(i).unwrap().keys.len() == MAX_KEYS {
        BTreeMap::split_child(node, i);
        // After split, the median key is at position i in this node.
        // Decide which child to descend into.
        match key.cmp(node.keys.get(i).unwrap()) {
          Ordering::Greater => { i = i + 1; },
          _ => {},
        }
      }
      BTreeMap::insert_non_full(node.children.get_mut(i).unwrap(), key, value);
    }
  }

  fn split_child(parent: refmut<BTreeNode<K, V>>, child_idx: usize): void {
    const child = parent.children.get_mut(child_idx).unwrap();
    const mid = B - 1; // median index

    // Create right sibling.
    let right = BTreeNode {
      keys: Vec::new(),
      values: Vec::new(),
      children: Vec::new(),
      is_leaf: child.is_leaf,
    };

    // Move keys[mid+1..] and values[mid+1..] to right.
    let i: usize = mid + 1;
    while i < child.keys.len() {
      right.keys.push(child.keys.get(i).unwrap().clone());
      right.values.push(child.values.get(i).unwrap().clone());
      i = i + 1;
    }

    // Move children[mid+1..] to right (if internal node).
    if !child.is_leaf {
      let j: usize = mid + 1;
      while j < child.children.len() {
        right.children.push(child.children.get(j).unwrap().clone());
        j = j + 1;
      }
    }

    // Extract median key/value.
    const median_key = child.keys.get(mid).unwrap().clone();
    const median_value = child.values.get(mid).unwrap().clone();

    // Truncate child to keys[0..mid], values[0..mid], children[0..mid+1].
    child.keys.truncate(mid);
    child.values.truncate(mid);
    if !child.is_leaf {
      child.children.truncate(mid + 1);
    }

    // Insert median into parent.
    parent.keys.insert(child_idx, median_key);
    parent.values.insert(child_idx, median_value);
    parent.children.insert(child_idx + 1, right);
  }

  fn remove(refmut<Self>, key: ref<K>): Option<own<V>> {
    match self.root {
      Option::None => { return Option::None; },
      Option::Some(ref root) => {
        const result = BTreeMap::remove_from_node(root, key);
        if result.is_some() {
          self.len = self.len - 1;
          // If root is empty and has children, replace with first child.
          if root.keys.is_empty() && !root.is_leaf {
            self.root = Option::Some(root.children.remove(0));
          }
        }
        return result;
      },
    }
  }

  fn remove_from_node(node: refmut<BTreeNode<K, V>>, key: ref<K>): Option<own<V>> {
    // Find key position.
    let i: usize = 0;
    while i < node.keys.len() {
      match key.cmp(node.keys.get(i).unwrap()) {
        Ordering::Equal => {
          if node.is_leaf {
            // Simple case: remove from leaf.
            node.keys.remove(i);
            return Option::Some(node.values.remove(i));
          } else {
            // Internal node: replace with in-order predecessor.
            const pred = BTreeMap::get_max(node.children.get_mut(i).unwrap());
            const pred_key = node.keys.remove(i);
            const pred_val = node.values.remove(i);
            node.keys.insert(i, pred.0);
            node.values.insert(i, pred.1);
            return Option::Some(pred_val);
          }
        },
        Ordering::Less => {
          if node.is_leaf { return Option::None; }
          return BTreeMap::remove_from_node(node.children.get_mut(i).unwrap(), key);
        },
        Ordering::Greater => { i = i + 1; },
      }
    }
    if node.is_leaf { return Option::None; }
    return BTreeMap::remove_from_node(node.children.get_mut(i).unwrap(), key);
  }

  fn get_max(node: refmut<BTreeNode<K, V>>): (own<K>, own<V>) {
    if node.is_leaf {
      const last = node.keys.len() - 1;
      const k = node.keys.remove(last);
      const v = node.values.remove(last);
      return (k, v);
    }
    const last_child = node.children.len() - 1;
    return BTreeMap::get_max(node.children.get_mut(last_child).unwrap());
  }

  fn first_key_value(ref<Self>): Option<(ref<K>, ref<V>)> {
    match self.root {
      Option::None => { return Option::None; },
      Option::Some(ref node) => { return BTreeMap::get_min_ref(node); },
    }
  }

  fn get_min_ref(node: ref<BTreeNode<K, V>>): Option<(ref<K>, ref<V>)> {
    if node.is_leaf {
      if node.keys.is_empty() { return Option::None; }
      return Option::Some((node.keys.get(0).unwrap(), node.values.get(0).unwrap()));
    }
    return BTreeMap::get_min_ref(node.children.get(0).unwrap());
  }

  fn last_key_value(ref<Self>): Option<(ref<K>, ref<V>)> {
    match self.root {
      Option::None => { return Option::None; },
      Option::Some(ref node) => { return BTreeMap::get_max_ref(node); },
    }
  }

  fn get_max_ref(node: ref<BTreeNode<K, V>>): Option<(ref<K>, ref<V>)> {
    if node.is_leaf {
      if node.keys.is_empty() { return Option::None; }
      const last = node.keys.len() - 1;
      return Option::Some((node.keys.get(last).unwrap(), node.values.get(last).unwrap()));
    }
    const last_child = node.children.len() - 1;
    return BTreeMap::get_max_ref(node.children.get(last_child).unwrap());
  }

  fn clear(refmut<Self>): void {
    self.root = Option::None;
    self.len = 0;
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `lit test/std/test_btreemap.ts -v`

Expected: PASS

- [ ] **Step 5: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: 238 tests (237 + 1 new), all pass.

### Task 6: Fix BTreeSet Stubs

**Files:**
- Modify: `std/collections/btreeset.ts`

- [ ] **Step 1: Replace btreeset.ts with working implementation**

```typescript
// std/collections/btreeset.ts — BTreeSet<T> (RFC-0013)
// Ordered set backed by a BTreeMap<T, ()>.

import { Ord } from '../core/cmp';

struct BTreeSet<T: Ord> {
  map: own<BTreeMap<T, ()>>,
}

impl<T: Ord> BTreeSet<T> {
  fn new(): own<BTreeSet<T>> {
    return BTreeSet { map: BTreeMap::new() };
  }

  fn len(ref<Self>): usize { return self.map.len(); }
  fn is_empty(ref<Self>): bool { return self.map.is_empty(); }

  fn insert(refmut<Self>, value: own<T>): bool {
    return self.map.insert(value, ()).is_none();
  }

  fn contains(ref<Self>, value: ref<T>): bool {
    return self.map.contains_key(value);
  }

  fn remove(refmut<Self>, value: ref<T>): bool {
    return self.map.remove(value).is_some();
  }

  fn first(ref<Self>): Option<ref<T>> {
    match self.map.first_key_value() {
      Option::Some(kv) => { return Option::Some(kv.0); },
      Option::None => { return Option::None; },
    }
  }

  fn last(ref<Self>): Option<ref<T>> {
    match self.map.last_key_value() {
      Option::Some(kv) => { return Option::Some(kv.0); },
      Option::None => { return Option::None; },
    }
  }

  fn clear(refmut<Self>): void { self.map.clear(); }
}
```

- [ ] **Step 2: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All tests pass (btreeset test uses BTreeMap now via delegation).

### Task 7: Commit Untracked Files and Phase 1 Changes

- [ ] **Step 1: Stage and commit all Phase 1 changes**

```bash
git add std/collections/btreemap.ts std/collections/btreeset.ts test/std/test_btreemap.ts
git commit -m "fix: real B-tree insert/remove for BTreeMap + BTreeSet delegation (RFC-0013)"
```

- [ ] **Step 2: Commit untracked std files**

```bash
git add std/async/throttle.ts std/collections/heap.ts std/collections/linked_list.ts \
  std/crypto/sha512.ts std/encoding/percent.ts std/sync/lazy.ts \
  test/std/test_percent.ts test/std/test_sha512.ts \
  std/path/posix.ts .gitignore
git commit -m "feat: add std throttle, heap, linked_list, sha512, percent, lazy + tests"
```

- [ ] **Step 3: Run full test suite to confirm clean state**

Run: `lit test/ --no-progress-bar`

Expected: All tests pass.

---

## Phase 2: RFC-0011 Core Traits

### Task 8: Register IntoIterator, FromIterator, IndexMut in Sema

**Files:**
- Modify: `lib/Sema/Builtins.cpp` (append before closing `}` of function, before line 927)

- [ ] **Step 1: Add IntoIterator trait registration**

Add before the closing `}` at line 927 of `Builtins.cpp`:

```cpp
  // IntoIterator trait: fn into_iter(own<Self>): own<IntoIter>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfType;
    selfParam.loc = loc;
    auto *intoIterMethod = ctx.create<FunctionDecl>(
        "into_iter", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam},
        ctx.create<NamedType>("IntoIter", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem intoIterItem;
    intoIterItem.method = intoIterMethod;
    TraitItem itemAssoc;
    itemAssoc.assocTypeName = "Item";
    itemAssoc.isAssocType = true;
    TraitItem iterAssoc;
    iterAssoc.assocTypeName = "IntoIter";
    iterAssoc.isAssocType = true;
    auto *intoIterTrait = ctx.create<TraitDecl>(
        "IntoIterator", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{itemAssoc, iterAssoc, intoIterItem}, loc);
    traitDecls["IntoIterator"] = intoIterTrait;
    Symbol sym;
    sym.name = "IntoIterator";
    sym.decl = intoIterTrait;
    scope->declare("IntoIterator", std::move(sym));
  }

  // FromIterator<T> trait: fn from_iter(iter): own<Self>
  {
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    GenericParam gpI;
    gpI.name = "I";
    gpI.loc = loc;
    ParamDecl iterParam;
    iterParam.name = "iter";
    iterParam.type = ctx.create<NamedType>("I", std::vector<Type *>{}, loc);
    iterParam.loc = loc;
    auto *fromIterMethod = ctx.create<FunctionDecl>(
        "from_iter", std::vector<GenericParam>{gpI},
        std::vector<ParamDecl>{iterParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem fromIterItem;
    fromIterItem.method = fromIterMethod;
    auto *fromIterTrait = ctx.create<TraitDecl>(
        "FromIterator", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{fromIterItem}, loc);
    traitDecls["FromIterator"] = fromIterTrait;
    Symbol sym;
    sym.name = "FromIterator";
    sym.decl = fromIterTrait;
    scope->declare("FromIterator", std::move(sym));
  }

  // IndexMut<Idx> trait: fn index_mut(refmut<Self>, Idx): refmut<Output>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRefMut = ctx.create<RefMutType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRefMut;
    selfParam.isSelfRefMut = true;
    selfParam.loc = loc;
    ParamDecl idxParam;
    idxParam.name = "index";
    idxParam.type = ctx.getBuiltinType(BuiltinTypeKind::USize);
    idxParam.loc = loc;
    auto *outputType = ctx.create<NamedType>("Output", std::vector<Type *>{}, loc);
    auto *refMutOutputType = ctx.create<RefMutType>(outputType, loc);
    auto *indexMutMethod = ctx.create<FunctionDecl>(
        "index_mut", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, idxParam},
        refMutOutputType, nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem indexMutItem;
    indexMutItem.method = indexMutMethod;
    TraitItem outputAssoc;
    outputAssoc.assocTypeName = "Output";
    outputAssoc.isAssocType = true;
    GenericParam gp;
    gp.name = "Output";
    gp.loc = loc;
    auto *indexMutTrait = ctx.create<TraitDecl>(
        "IndexMut", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{outputAssoc, indexMutItem}, loc);
    traitDecls["IndexMut"] = indexMutTrait;
    Symbol sym;
    sym.name = "IndexMut";
    sym.decl = indexMutTrait;
    scope->declare("IndexMut", std::move(sym));
  }
```

- [ ] **Step 2: Rebuild and test**

Run: `cmake --build build/ -j$(sysctl -n hw.ncpu) && lit test/ --no-progress-bar`

Expected: Build succeeds. All tests pass.

- [ ] **Step 3: Commit**

```bash
git add lib/Sema/Builtins.cpp
git commit -m "feat: register IntoIterator/FromIterator/IndexMut traits in Sema (RFC-0011)"
```

### Task 9: Add Iterator Combinators — max, min, Peekable

**Files:**
- Modify: `std/core/iter.ts` (append before the Range<i32> impl at line 249)

- [ ] **Step 1: Write the test**

Create `test/std/test_iter_combinators.ts`:

```typescript
// RUN: %asc check %s
// Test: Iterator combinators — max, min, peekable.
function main(): i32 {
  // max/min via Vec iterator.
  let v: Vec<i32> = Vec::new();
  v.push(3);
  v.push(1);
  v.push(4);
  v.push(1);
  v.push(5);

  // Test max.
  const mx = v.iter().max();
  assert!(mx.is_some());
  assert_eq!(mx.unwrap(), 5);

  // Test min.
  const mn = v.iter().min();
  assert!(mn.is_some());
  assert_eq!(mn.unwrap(), 1);

  // Test max on empty.
  let empty: Vec<i32> = Vec::new();
  assert!(empty.iter().max().is_none());

  // Test peekable.
  let v2: Vec<i32> = Vec::new();
  v2.push(10);
  v2.push(20);
  let pk = v2.iter().peekable();
  assert_eq!(pk.peek().unwrap(), 10);
  assert_eq!(pk.peek().unwrap(), 10);  // peek again — same value.
  assert_eq!(pk.next().unwrap(), 10);
  assert_eq!(pk.next().unwrap(), 20);
  assert!(pk.next().is_none());

  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/std/test_iter_combinators.ts -v`

Expected: FAIL (max, min, peekable not defined)

- [ ] **Step 3: Add max, min, peekable to Iterator trait and Peekable struct**

In `std/core/iter.ts`, add these methods inside the `trait Iterator` block (before the closing `}` at line 104):

```typescript
  fn max(own<Self>): Option<own<Item>> where Item: Ord {
    let result: Option<own<Item>> = Option::None;
    loop {
      match self.next() {
        Option::Some(v) => {
          match result {
            Option::None => { result = Option::Some(v); },
            Option::Some(ref current) => {
              match v.cmp(current) {
                Ordering::Greater => { result = Option::Some(v); },
                _ => {},
              }
            },
          }
        },
        Option::None => { return result; },
      }
    }
  }

  fn min(own<Self>): Option<own<Item>> where Item: Ord {
    let result: Option<own<Item>> = Option::None;
    loop {
      match self.next() {
        Option::Some(v) => {
          match result {
            Option::None => { result = Option::Some(v); },
            Option::Some(ref current) => {
              match v.cmp(current) {
                Ordering::Less => { result = Option::Some(v); },
                _ => {},
              }
            },
          }
        },
        Option::None => { return result; },
      }
    }
  }

  fn peekable(own<Self>): own<Peekable<Self>> {
    return Peekable { iter: self, peeked: Option::None };
  }
```

Then add the `Peekable` struct after the `Zip` impl (after line 247):

```typescript
/// Iterator adapter that caches one element for peeking.
struct Peekable<I: Iterator> {
  iter: own<I>,
  peeked: Option<own<I::Item>>,
}

impl<I: Iterator> Peekable<I> {
  fn peek(refmut<Self>): Option<ref<I::Item>> {
    if self.peeked.is_none() {
      self.peeked = self.iter.next();
    }
    match self.peeked {
      Option::Some(ref v) => { return Option::Some(v); },
      Option::None => { return Option::None; },
    }
  }
}

impl<I: Iterator> Iterator for Peekable<I> {
  type Item = I::Item;
  fn next(refmut<Self>): Option<own<I::Item>> {
    match self.peeked.take() {
      Option::Some(v) => { return Option::Some(v); },
      Option::None => { return self.iter.next(); },
    }
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `lit test/std/test_iter_combinators.ts -v`

Expected: PASS

- [ ] **Step 5: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add std/core/iter.ts test/std/test_iter_combinators.ts
git commit -m "feat: add Iterator max/min/peekable combinators (RFC-0011)"
```

### Task 10: Fix Display Implementations for Primitives

**Files:**
- Modify: `std/core/fmt.ts:72-108`

- [ ] **Step 1: Replace i32 Display impl with digit extraction**

Replace the Display impl for i32 (lines 72-77) with:

```typescript
impl Display for i32 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    let n = *self;
    if n == 0 { return f.write_str("0"); }
    let buf: [u8; 11] = [0; 11]; // max i32 is 10 digits + sign
    let pos: usize = 11;
    let neg = false;
    if n < 0 { neg = true; n = 0 - n; }
    while n > 0 {
      pos = pos - 1;
      buf[pos] = (48 + (n % 10)) as u8; // '0' = 48
      n = n / 10;
    }
    if neg {
      pos = pos - 1;
      buf[pos] = 45; // '-'
    }
    const s = unsafe { str::from_raw_parts(&buf[pos] as *const u8, 11 - pos) };
    return f.write_str(s);
  }
}
```

- [ ] **Step 2: Replace i64 Display impl**

Replace the Display impl for i64 (lines 79-83) with:

```typescript
impl Display for i64 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    let n = *self;
    if n == 0 { return f.write_str("0"); }
    let buf: [u8; 20] = [0; 20]; // max i64 is 19 digits + sign
    let pos: usize = 20;
    let neg = false;
    if n < 0 { neg = true; n = 0 - n; }
    while n > 0 {
      pos = pos - 1;
      buf[pos] = (48 + (n % 10) as u8);
      n = n / 10;
    }
    if neg {
      pos = pos - 1;
      buf[pos] = 45;
    }
    const s = unsafe { str::from_raw_parts(&buf[pos] as *const u8, 20 - pos) };
    return f.write_str(s);
  }
}
```

- [ ] **Step 3: Replace f64 Display impl (simplified decimal)**

Replace the Display impl for f64 (lines 85-89) with:

```typescript
impl Display for f64 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    let val = *self;
    if val < 0.0 {
      f.write_char('-');
      val = 0.0 - val;
    }
    // Integer part.
    let int_part = val as i64;
    let frac = val - (int_part as f64);
    // Write integer part.
    let ibuf: [u8; 20] = [0; 20];
    let ipos: usize = 20;
    if int_part == 0 {
      ipos = ipos - 1;
      ibuf[ipos] = 48;
    } else {
      while int_part > 0 {
        ipos = ipos - 1;
        ibuf[ipos] = (48 + (int_part % 10) as u8);
        int_part = int_part / 10;
      }
    }
    const is = unsafe { str::from_raw_parts(&ibuf[ipos] as *const u8, 20 - ipos) };
    f.write_str(is);
    // Decimal point + 6 fractional digits.
    f.write_char('.');
    let digits: usize = 6;
    let fbuf: [u8; 6] = [0; 6];
    let d: usize = 0;
    while d < digits {
      frac = frac * 10.0;
      const digit = frac as i32;
      fbuf[d] = (48 + digit) as u8;
      frac = frac - (digit as f64);
      d = d + 1;
    }
    const fs = unsafe { str::from_raw_parts(&fbuf[0] as *const u8, digits) };
    return f.write_str(fs);
  }
}
```

- [ ] **Step 4: Replace Debug for i32 with real formatting too**

Replace the Debug impl for i32 (lines 104-108) with:

```typescript
impl Debug for i32 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    // Reuse Display for now.
    return Display::fmt(self, f);
  }
}
```

- [ ] **Step 5: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add std/core/fmt.ts
git commit -m "fix: Display impls for i32/i64/f64 now emit actual values (RFC-0011)"
```

---

## Phase 3: RFC-0013 Collections/String

### Task 11: Add Vec sort, sort_by, retain, truncate, iter, iter_mut

**Files:**
- Modify: `std/vec.ts` (add methods before the Drop impl at line 177)

- [ ] **Step 1: Write the test**

Create `test/std/test_vec_sort.ts`:

```typescript
// RUN: %asc check %s
// Test: Vec sort, retain, truncate, iter.
function main(): i32 {
  // sort.
  let v: Vec<i32> = Vec::new();
  v.push(5);
  v.push(2);
  v.push(8);
  v.push(1);
  v.push(3);
  v.sort();
  assert_eq!(*v.get(0).unwrap(), 1);
  assert_eq!(*v.get(1).unwrap(), 2);
  assert_eq!(*v.get(2).unwrap(), 3);
  assert_eq!(*v.get(3).unwrap(), 5);
  assert_eq!(*v.get(4).unwrap(), 8);

  // retain — keep only even numbers.
  v.retain((x: ref<i32>) => *x % 2 == 0);
  assert_eq!(v.len(), 2);
  assert_eq!(*v.get(0).unwrap(), 2);
  assert_eq!(*v.get(1).unwrap(), 8);

  // truncate.
  v.push(10);
  v.push(12);
  v.truncate(2);
  assert_eq!(v.len(), 2);

  // truncate to larger — no-op.
  v.truncate(100);
  assert_eq!(v.len(), 2);

  // iter.
  let v2: Vec<i32> = Vec::new();
  v2.push(1);
  v2.push(2);
  v2.push(3);
  let sum: i32 = 0;
  let it = v2.iter();
  loop {
    match it.next() {
      Option::Some(val) => { sum = sum + *val; },
      Option::None => { break; },
    }
  }
  assert_eq!(sum, 6);

  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/std/test_vec_sort.ts -v`

Expected: FAIL (sort, retain, iter not defined)

- [ ] **Step 3: Add sort, sort_by, retain, truncate methods**

In `std/vec.ts`, add these methods after `extend()` (after line 160, before `grow()`):

```typescript
  fn sort(refmut<Self>): void where T: Ord {
    // Insertion sort — simple, correct, O(n^2). Good enough for std library.
    if self.len <= 1 { return; }
    let i: usize = 1;
    const elem_size = size_of!<T>();
    while i < self.len {
      let j: usize = i;
      while j > 0 {
        const curr = (self.ptr as usize + j * elem_size) as *const T;
        const prev = (self.ptr as usize + (j - 1) * elem_size) as *const T;
        match (unsafe { &*curr }).cmp(unsafe { &*prev }) {
          Ordering::Less => {
            // Swap bytes.
            let a = (self.ptr as usize + j * elem_size) as *mut u8;
            let b = (self.ptr as usize + (j - 1) * elem_size) as *mut u8;
            let k: usize = 0;
            while k < elem_size {
              const tmp = a[k];
              a[k] = b[k];
              b[k] = tmp;
              k = k + 1;
            }
            j = j - 1;
          },
          _ => { break; },
        }
      }
      i = i + 1;
    }
  }

  fn sort_by(refmut<Self>, cmp: (ref<T>, ref<T>) -> Ordering): void {
    if self.len <= 1 { return; }
    let i: usize = 1;
    const elem_size = size_of!<T>();
    while i < self.len {
      let j: usize = i;
      while j > 0 {
        const curr = (self.ptr as usize + j * elem_size) as *const T;
        const prev = (self.ptr as usize + (j - 1) * elem_size) as *const T;
        match cmp(unsafe { &*curr }, unsafe { &*prev }) {
          Ordering::Less => {
            let a = (self.ptr as usize + j * elem_size) as *mut u8;
            let b = (self.ptr as usize + (j - 1) * elem_size) as *mut u8;
            let k: usize = 0;
            while k < elem_size {
              const tmp = a[k];
              a[k] = b[k];
              b[k] = tmp;
              k = k + 1;
            }
            j = j - 1;
          },
          _ => { break; },
        }
      }
      i = i + 1;
    }
  }

  fn retain(refmut<Self>, f: (ref<T>) -> bool): void {
    let write: usize = 0;
    let read: usize = 0;
    const elem_size = size_of!<T>();
    while read < self.len {
      const slot = (self.ptr as usize + read * elem_size) as *const T;
      if f(unsafe { &*slot }) {
        if write != read {
          const dst = (self.ptr as usize + write * elem_size) as *mut u8;
          const src = (self.ptr as usize + read * elem_size) as *const u8;
          memcpy(dst, src, elem_size);
        }
        write = write + 1;
      }
      read = read + 1;
    }
    self.len = write;
  }

  fn truncate(refmut<Self>, new_len: usize): void {
    if new_len >= self.len { return; }
    self.len = new_len;
  }

  fn iter(ref<Self>): own<VecIter<T>> {
    const elem_size = size_of!<T>();
    const end_ptr = (self.ptr as usize + self.len * elem_size) as *const T;
    return VecIter { ptr: self.ptr as *const T, end: end_ptr };
  }

  fn iter_mut(refmut<Self>): own<VecIterMut<T>> {
    const elem_size = size_of!<T>();
    const end_ptr = (self.ptr as usize + self.len * elem_size) as *mut T;
    return VecIterMut { ptr: self.ptr, end: end_ptr };
  }
```

- [ ] **Step 4: Add VecIter and VecIterMut structs**

Add after the `Clone for Vec<T>` impl (at end of file, after line 215):

```typescript
/// Borrowing iterator over Vec<T>.
struct VecIter<T> {
  ptr: *const T,
  end: *const T,
}

impl<T> Iterator for VecIter<T> {
  type Item = ref<T>;
  fn next(refmut<Self>): Option<ref<T>> {
    if self.ptr as usize >= self.end as usize { return Option::None; }
    const elem_size = size_of!<T>();
    const current = self.ptr;
    self.ptr = (self.ptr as usize + elem_size) as *const T;
    return Option::Some(unsafe { &*current });
  }
}

/// Mutable borrowing iterator over Vec<T>.
struct VecIterMut<T> {
  ptr: *mut T,
  end: *mut T,
}

impl<T> Iterator for VecIterMut<T> {
  type Item = refmut<T>;
  fn next(refmut<Self>): Option<refmut<T>> {
    if self.ptr as usize >= self.end as usize { return Option::None; }
    const elem_size = size_of!<T>();
    const current = self.ptr;
    self.ptr = (self.ptr as usize + elem_size) as *mut T;
    return Option::Some(unsafe { &mut *current });
  }
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `lit test/std/test_vec_sort.ts -v`

Expected: PASS

- [ ] **Step 6: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All tests pass.

- [ ] **Step 7: Commit**

```bash
git add std/vec.ts test/std/test_vec_sort.ts
git commit -m "feat: Vec sort/sort_by/retain/truncate/iter/iter_mut (RFC-0013)"
```

### Task 12: Add String find, trim, split, replace, as_bytes, chars

**Files:**
- Modify: `std/string.ts` (add methods before `ensure_capacity` at line 103)

- [ ] **Step 1: Write the test**

Create `test/std/test_string_methods.ts`:

```typescript
// RUN: %asc check %s
// Test: String find, trim, split, replace.
function main(): i32 {
  let s = String::from("  hello world  ");

  // trim.
  const trimmed = s.trim();
  assert_eq!(trimmed.len(), 11); // "hello world"

  // trim_start.
  const ts = s.trim_start();
  assert_eq!(ts.len(), 13); // "hello world  "

  // trim_end.
  const te = s.trim_end();
  assert_eq!(te.len(), 13); // "  hello world"

  // find.
  let s2 = String::from("abcdef");
  assert_eq!(s2.find("cd").unwrap(), 2);
  assert!(s2.find("xyz").is_none());
  assert_eq!(s2.find("a").unwrap(), 0);

  // split.
  let s3 = String::from("a,b,c");
  const parts = s3.split(",");
  assert_eq!(parts.len(), 3);

  // replace.
  let s4 = String::from("hello world");
  const replaced = s4.replace("world", "asc");
  assert_eq!(replaced.len(), 9); // "hello asc"

  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/std/test_string_methods.ts -v`

Expected: FAIL (find, trim, split, replace not defined)

- [ ] **Step 3: Add methods to String**

In `std/string.ts`, add these methods before `ensure_capacity` (before line 103):

```typescript
  fn find(ref<Self>, pattern: ref<str>): Option<usize> {
    const plen = pattern.len();
    if plen == 0 { return Option::Some(0); }
    if plen > self.len { return Option::None; }
    let i: usize = 0;
    const limit = self.len - plen + 1;
    const pptr = pattern.as_ptr();
    while i < limit {
      let matched = true;
      let j: usize = 0;
      while j < plen {
        if self.ptr[i + j] != pptr[j] { matched = false; break; }
        j = j + 1;
      }
      if matched { return Option::Some(i); }
      i = i + 1;
    }
    return Option::None;
  }

  fn trim(ref<Self>): ref<str> {
    let start: usize = 0;
    while start < self.len && Self::is_ascii_whitespace(self.ptr[start]) {
      start = start + 1;
    }
    let end: usize = self.len;
    while end > start && Self::is_ascii_whitespace(self.ptr[end - 1]) {
      end = end - 1;
    }
    return unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, end - start) };
  }

  fn trim_start(ref<Self>): ref<str> {
    let start: usize = 0;
    while start < self.len && Self::is_ascii_whitespace(self.ptr[start]) {
      start = start + 1;
    }
    return unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, self.len - start) };
  }

  fn trim_end(ref<Self>): ref<str> {
    let end: usize = self.len;
    while end > 0 && Self::is_ascii_whitespace(self.ptr[end - 1]) {
      end = end - 1;
    }
    return unsafe { str::from_raw_parts(self.ptr as *const u8, end) };
  }

  fn is_ascii_whitespace(c: u8): bool {
    return c == 32 || c == 9 || c == 10 || c == 13; // space, tab, newline, carriage return
  }

  fn split(ref<Self>, sep: ref<str>): own<Vec<ref<str>>> {
    let result: Vec<ref<str>> = Vec::new();
    const slen = sep.len();
    if slen == 0 {
      result.push(self.as_str());
      return result;
    }
    let start: usize = 0;
    let i: usize = 0;
    const sep_ptr = sep.as_ptr();
    while i + slen <= self.len {
      let matched = true;
      let j: usize = 0;
      while j < slen {
        if self.ptr[i + j] != sep_ptr[j] { matched = false; break; }
        j = j + 1;
      }
      if matched {
        const part = unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, i - start) };
        result.push(part);
        start = i + slen;
        i = start;
      } else {
        i = i + 1;
      }
    }
    const last_part = unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, self.len - start) };
    result.push(last_part);
    return result;
  }

  fn replace(ref<Self>, from: ref<str>, to: ref<str>): own<String> {
    let result = String::new();
    const flen = from.len();
    if flen == 0 {
      result.push_str(self.as_str());
      return result;
    }
    let start: usize = 0;
    let i: usize = 0;
    const from_ptr = from.as_ptr();
    while i + flen <= self.len {
      let matched = true;
      let j: usize = 0;
      while j < flen {
        if self.ptr[i + j] != from_ptr[j] { matched = false; break; }
        j = j + 1;
      }
      if matched {
        const segment = unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, i - start) };
        result.push_str(segment);
        result.push_str(to);
        start = i + flen;
        i = start;
      } else {
        i = i + 1;
      }
    }
    const tail = unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, self.len - start) };
    result.push_str(tail);
    return result;
  }

  fn as_bytes(ref<Self>): ref<[u8]> {
    return unsafe { slice::from_raw_parts(self.ptr as *const u8, self.len) };
  }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `lit test/std/test_string_methods.ts -v`

Expected: PASS

- [ ] **Step 5: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add std/string.ts test/std/test_string_methods.ts
git commit -m "feat: String find/trim/split/replace/as_bytes (RFC-0013)"
```

### Task 13: Add HashMap get_mut, keys, values, Entry API

**Files:**
- Modify: `std/collections/hashmap.ts` (add methods before Drop impl at line 137)

- [ ] **Step 1: Write the test**

Create `test/std/test_hashmap_entry.ts`:

```typescript
// RUN: %asc check %s
// Test: HashMap get_mut, keys, values.
function main(): i32 {
  let map: HashMap<i32, i32> = HashMap::new();
  map.insert(1, 100);
  map.insert(2, 200);
  map.insert(3, 300);

  // get_mut — modify value in place.
  let val = map.get_mut(2).unwrap();
  *val = 999;
  assert_eq!(*map.get(2).unwrap(), 999);

  // keys.
  const ks = map.keys();
  assert_eq!(ks.len(), 3);

  // values.
  const vs = map.values();
  assert_eq!(vs.len(), 3);

  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/std/test_hashmap_entry.ts -v`

Expected: FAIL (get_mut, keys, values not defined)

- [ ] **Step 3: Add get_mut, keys, values methods**

In `std/collections/hashmap.ts`, add before the `clear` method (before line 126):

```typescript
  fn get_mut(refmut<Self>, key: ref<K>): Option<refmut<V>> {
    const hash = self.hash_key(key);
    const cap = self.buckets.len();
    let idx = (hash as usize) % cap;

    let i: usize = 0;
    while i < cap {
      const bucket_idx = (idx + i) % cap;
      const bucket = self.buckets.get_mut(bucket_idx).unwrap();
      if !bucket.occupied { return Option::None; }
      if bucket.hash == hash && bucket.key.eq(key) {
        return Option::Some(&mut bucket.value);
      }
      i = i + 1;
    }
    return Option::None;
  }

  fn keys(ref<Self>): own<Vec<ref<K>>> {
    let result: Vec<ref<K>> = Vec::new();
    let i: usize = 0;
    while i < self.buckets.len() {
      const bucket = self.buckets.get(i).unwrap();
      if bucket.occupied {
        result.push(&bucket.key);
      }
      i = i + 1;
    }
    return result;
  }

  fn values(ref<Self>): own<Vec<ref<V>>> {
    let result: Vec<ref<V>> = Vec::new();
    let i: usize = 0;
    while i < self.buckets.len() {
      const bucket = self.buckets.get(i).unwrap();
      if bucket.occupied {
        result.push(&bucket.value);
      }
      i = i + 1;
    }
    return result;
  }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `lit test/std/test_hashmap_entry.ts -v`

Expected: PASS

- [ ] **Step 5: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add std/collections/hashmap.ts test/std/test_hashmap_entry.ts
git commit -m "feat: HashMap get_mut/keys/values (RFC-0013)"
```

### Task 14: Final Validation and Summary Commit

- [ ] **Step 1: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All tests pass (237 original + 5 new = 242+).

- [ ] **Step 2: Verify no uncommitted changes**

Run: `git status`

Expected: Clean working tree.

- [ ] **Step 3: Count test files for audit update**

Run: `find test/ -name "*.ts" | wc -l`

Record the count for the RFC audit update.
