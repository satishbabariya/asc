// RUN: %asc check %s
// Tests newly wired stdlib methods: pop, is_empty, clear, remove, as_str.

function test_vec(): i32 {
  let v: Vec<i32> = Vec::new();
  v.push(10);
  v.push(20);
  let popped = v.pop();
  let empty = v.is_empty();
  v.clear();
  return v.len() as i32;
}

function test_hashmap(): i32 {
  let m: HashMap<i32, i32> = HashMap::new();
  m.insert(1, 100);
  m.insert(2, 200);
  m.remove(1);
  return m.len() as i32;
}

function test_string(): i32 {
  let s: String = String::new();
  s.push_str("hello");
  let ptr = s.as_ptr();
  s.clear();
  return s.len() as i32;
}

function main(): i32 {
  return test_vec();
}
