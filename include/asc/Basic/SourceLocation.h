#ifndef ASC_BASIC_SOURCELOCATION_H
#define ASC_BASIC_SOURCELOCATION_H

#include <cstdint>

namespace asc {

/// Unique identifier for a source file managed by SourceManager.
class FileID {
  uint32_t ID = 0;

public:
  FileID() = default;
  explicit FileID(uint32_t id) : ID(id) {}

  uint32_t getID() const { return ID; }
  bool isValid() const { return ID != 0; }
  bool operator==(const FileID &other) const { return ID == other.ID; }
  bool operator!=(const FileID &other) const { return ID != other.ID; }
  bool operator<(const FileID &other) const { return ID < other.ID; }
};

/// A (FileID, Offset) pair identifying a position in source code.
/// Modelled after Clang's SourceLocation.
class SourceLocation {
  uint32_t fileID = 0;
  uint32_t offset = 0;

public:
  SourceLocation() = default;
  SourceLocation(FileID fid, uint32_t off)
      : fileID(fid.getID()), offset(off) {}

  FileID getFileID() const { return FileID(fileID); }
  uint32_t getOffset() const { return offset; }
  bool isValid() const { return fileID != 0; }
  bool isInvalid() const { return fileID == 0; }

  bool operator==(const SourceLocation &other) const {
    return fileID == other.fileID && offset == other.offset;
  }
  bool operator!=(const SourceLocation &other) const {
    return !(*this == other);
  }
};

/// A half-open range [Begin, End) in source code.
class SourceRange {
  SourceLocation begin;
  SourceLocation end;

public:
  SourceRange() = default;
  SourceRange(SourceLocation b, SourceLocation e) : begin(b), end(e) {}
  explicit SourceRange(SourceLocation loc) : begin(loc), end(loc) {}

  SourceLocation getBegin() const { return begin; }
  SourceLocation getEnd() const { return end; }
  bool isValid() const { return begin.isValid(); }
};

} // namespace asc

#endif // ASC_BASIC_SOURCELOCATION_H
