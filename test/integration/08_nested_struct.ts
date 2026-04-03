// test 08: nested struct field access
struct Vec2 {
  x: i32,
  y: i32,
}

struct Line {
  start: Vec2,
  end: Vec2,
}

function endpoint_sum(l: Line): i32 {
  return l.end.x + l.end.y;
}

function main(): i32 {
  let l = Line {
    start: Vec2 { x: 0, y: 0 },
    end: Vec2 { x: 10, y: 15 },
  };
  return endpoint_sum(l);
}
