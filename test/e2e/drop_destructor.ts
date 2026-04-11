// RUN: %asc check %s

struct Resource { id: i32 }

impl Drop for Resource {
  function drop(self: refmut<Resource>): void {
    // Custom cleanup
  }
}

function main(): i32 {
  let r = Resource { id: 42 };
  return r.id;
}
