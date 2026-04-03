#ifndef ASC_BASIC_SOURCEMANAGER_H
#define ASC_BASIC_SOURCEMANAGER_H

#include "asc/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <string>

namespace asc {

/// Information about a line/column position.
struct LineColumnInfo {
  unsigned line;   // 1-based
  unsigned column; // 1-based
};

/// Manages source file buffers and maps offsets to line/column info.
class SourceManager {
public:
  SourceManager();
  ~SourceManager();

  /// Load a file from disk. Returns invalid FileID on failure.
  FileID loadFile(llvm::StringRef filename);

  /// Create a buffer from a string (for tests). Returns the assigned FileID.
  FileID createBuffer(llvm::StringRef name, llvm::StringRef content);

  /// Get the filename for a FileID.
  llvm::StringRef getFilename(FileID fid) const;

  /// Get the full buffer content for a FileID.
  llvm::StringRef getBufferData(FileID fid) const;

  /// Get the line and column for a SourceLocation.
  LineColumnInfo getLineAndColumn(SourceLocation loc) const;

  /// Get the source line text containing a SourceLocation.
  llvm::StringRef getSourceLine(SourceLocation loc) const;

  /// Get the number of loaded files.
  unsigned getNumFiles() const { return files.size(); }

private:
  struct FileEntry {
    std::string filename;
    std::string content;
    /// Cached line start offsets (lazily computed).
    mutable llvm::SmallVector<uint32_t, 64> lineOffsets;
    mutable bool lineOffsetsComputed = false;
  };

  void computeLineOffsets(const FileEntry &entry) const;

  llvm::SmallVector<FileEntry, 4> files; // index 0 unused (FileID starts at 1)
};

} // namespace asc

#endif // ASC_BASIC_SOURCEMANAGER_H
