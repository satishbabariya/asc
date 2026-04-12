// RUN: %asc check %s

trait Deref {
  function deref(self: ref<Self>): i32;
}

struct SmartPtr { value: i32 }

impl Deref for SmartPtr {
  function deref(self: ref<SmartPtr>): i32 {
    return self.value;
  }
}

function main(): i32 {
  return 0;
}
