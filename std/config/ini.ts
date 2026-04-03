// std/config/ini.ts — INI format parser/serializer (RFC-0019)

/// Represents an INI file as sections containing key-value pairs.
/// The empty-string section holds top-level (sectionless) entries.
struct IniDocument {
  sections: own<Vec<IniSection>>,
}

struct IniSection {
  name: own<String>,
  entries: own<Vec<(own<String>, own<String>)>>,
}

/// Error type for INI parsing.
enum IniError {
  InvalidLine(usize, own<String>),
  UnclosedSection(usize),
}

impl Display for IniError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      IniError::InvalidLine(line, msg) => {
        f.write_str("line ")?; line.fmt(f)?; f.write_str(": ")?; f.write_str(msg.as_str())
      },
      IniError::UnclosedSection(line) => f.write_str("unclosed section at line ").and_then(|_| line.fmt(f)),
    }
  }
}

impl IniDocument {
  fn new(): own<IniDocument> {
    let sections: own<Vec<IniSection>> = Vec::new();
    sections.push(IniSection { name: String::new(), entries: Vec::new() });
    return IniDocument { sections: sections };
  }

  /// Get a value by section and key.
  fn get(ref<Self>, section: ref<str>, key: ref<str>): Option<ref<str>> {
    let i: usize = 0;
    while i < self.sections.len() {
      let sec = self.sections.get(i).unwrap();
      if sec.name.as_str() == section {
        let j: usize = 0;
        while j < sec.entries.len() {
          let entry = sec.entries.get(j).unwrap();
          if entry.0.as_str() == key {
            return Option::Some(entry.1.as_str());
          }
          j = j + 1;
        }
      }
      i = i + 1;
    }
    return Option::None;
  }

  /// Set a value (creates section if needed).
  fn set(refmut<Self>, section: ref<str>, key: ref<str>, value: ref<str>): void {
    let sec_idx: Option<usize> = Option::None;
    let i: usize = 0;
    while i < self.sections.len() {
      if self.sections.get(i).unwrap().name.as_str() == section {
        sec_idx = Option::Some(i);
        break;
      }
      i = i + 1;
    }
    if sec_idx.is_none() {
      self.sections.push(IniSection {
        name: String::from(section),
        entries: Vec::new(),
      });
      sec_idx = Option::Some(self.sections.len() - 1);
    }
    let sec = self.sections.get_mut(sec_idx.unwrap()).unwrap();
    let j: usize = 0;
    while j < sec.entries.len() {
      if sec.entries.get(j).unwrap().0.as_str() == key {
        let entry = sec.entries.get_mut(j).unwrap();
        entry.1 = String::from(value);
        return;
      }
      j = j + 1;
    }
    sec.entries.push((String::from(key), String::from(value)));
  }

  /// Get all section names.
  fn section_names(ref<Self>): own<Vec<own<String>>> {
    let result: own<Vec<own<String>>> = Vec::new();
    let i: usize = 0;
    while i < self.sections.len() {
      let sec = self.sections.get(i).unwrap();
      if sec.name.len() > 0 {
        result.push(sec.name.clone());
      }
      i = i + 1;
    }
    return result;
  }
}

/// Parse an INI string.
function parse(input: ref<str>): Result<own<IniDocument>, IniError> {
  let doc = IniDocument::new();
  let current_section: usize = 0;
  let lines = input.split('\n');
  let line_num: usize = 0;

  let i: usize = 0;
  while i < lines.len() {
    line_num = line_num + 1;
    let line = trim(lines.get(i).unwrap().as_str());

    if line.len() == 0 || line.as_bytes()[0] == 0x3B || line.as_bytes()[0] == 0x23 {
      i = i + 1;
      continue;
    }

    // Section header: [section]
    if line.as_bytes()[0] == 0x5B {
      let end = find_byte(line, 0x5D);
      if end.is_none() {
        return Result::Err(IniError::UnclosedSection(line_num));
      }
      let name = String::from(line.slice(1, end.unwrap()));
      let found = false;
      let j: usize = 0;
      while j < doc.sections.len() {
        if doc.sections.get(j).unwrap().name.as_str() == name.as_str() {
          current_section = j;
          found = true;
          break;
        }
        j = j + 1;
      }
      if !found {
        doc.sections.push(IniSection { name: name, entries: Vec::new() });
        current_section = doc.sections.len() - 1;
      }
      i = i + 1;
      continue;
    }

    // Key=Value pair.
    let eq = find_byte(line, 0x3D);
    if eq.is_none() {
      return Result::Err(IniError::InvalidLine(line_num, String::from("missing '='")));
    }
    let key = trim(line.slice(0, eq.unwrap()));
    let value = trim(line.slice(eq.unwrap() + 1, line.len()));
    let val_str = strip_quotes(value);

    let sec = doc.sections.get_mut(current_section).unwrap();
    sec.entries.push((String::from(key), String::from(val_str)));
    i = i + 1;
  }

  return Result::Ok(doc);
}

/// Serialize an IniDocument to a string.
function stringify(doc: ref<IniDocument>): own<String> {
  let buf = String::new();
  let i: usize = 0;
  while i < doc.sections.len() {
    let sec = doc.sections.get(i).unwrap();
    if sec.name.len() > 0 {
      if buf.len() > 0 { buf.push(0x0A as char); }
      buf.push('[' as char);
      buf.push_str(sec.name.as_str());
      buf.push(']' as char);
      buf.push(0x0A as char);
    }
    let j: usize = 0;
    while j < sec.entries.len() {
      let entry = sec.entries.get(j).unwrap();
      buf.push_str(entry.0.as_str());
      buf.push_str(" = ");
      buf.push_str(entry.1.as_str());
      buf.push(0x0A as char);
      j = j + 1;
    }
    i = i + 1;
  }
  return buf;
}

// --- Internal helpers ---

function trim(s: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let start: usize = 0;
  while start < bytes.len() && (bytes[start] == 0x20 || bytes[start] == 0x09 || bytes[start] == 0x0D) {
    start = start + 1;
  }
  let end = bytes.len();
  while end > start && (bytes[end - 1] == 0x20 || bytes[end - 1] == 0x09 || bytes[end - 1] == 0x0D) {
    end = end - 1;
  }
  return s.slice(start, end);
}

function find_byte(s: ref<str>, b: u8): Option<usize> {
  let bytes = s.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    if bytes[i] == b { return Option::Some(i); }
    i = i + 1;
  }
  return Option::None;
}

function strip_quotes(s: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let len = bytes.len();
  if len >= 2 {
    let first = bytes[0];
    let last = bytes[len - 1];
    if (first == 0x22 && last == 0x22) || (first == 0x27 && last == 0x27) {
      return s.slice(1, len - 1);
    }
  }
  return s;
}
