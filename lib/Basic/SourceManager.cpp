#include "asc/Basic/SourceManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cassert>

namespace asc {

SourceManager::SourceManager() {
  // Index 0 is reserved (invalid FileID).
  files.emplace_back();
}

SourceManager::~SourceManager() = default;

FileID SourceManager::loadFile(llvm::StringRef filename) {
  auto bufOrErr =
      llvm::MemoryBuffer::getFile(filename, /*IsText=*/true);
  if (!bufOrErr)
    return FileID();

  uint32_t id = static_cast<uint32_t>(files.size());
  files.emplace_back();
  auto &entry = files.back();
  entry.filename = filename.str();
  entry.content = (*bufOrErr)->getBuffer().str();
  return FileID(id);
}

FileID SourceManager::createBuffer(llvm::StringRef name,
                                   llvm::StringRef content) {
  uint32_t id = static_cast<uint32_t>(files.size());
  files.emplace_back();
  auto &entry = files.back();
  entry.filename = name.str();
  entry.content = content.str();
  return FileID(id);
}

llvm::StringRef SourceManager::getFilename(FileID fid) const {
  assert(fid.isValid() && fid.getID() < files.size());
  return files[fid.getID()].filename;
}

llvm::StringRef SourceManager::getBufferData(FileID fid) const {
  assert(fid.isValid() && fid.getID() < files.size());
  return files[fid.getID()].content;
}

void SourceManager::computeLineOffsets(const FileEntry &entry) const {
  if (entry.lineOffsetsComputed)
    return;
  entry.lineOffsets.push_back(0); // Line 1 starts at offset 0.
  for (uint32_t i = 0, n = static_cast<uint32_t>(entry.content.size()); i < n;
       ++i) {
    if (entry.content[i] == '\n')
      entry.lineOffsets.push_back(i + 1);
  }
  entry.lineOffsetsComputed = true;
}

LineColumnInfo SourceManager::getLineAndColumn(SourceLocation loc) const {
  assert(loc.isValid());
  FileID fid = loc.getFileID();
  assert(fid.getID() < files.size());
  const auto &entry = files[fid.getID()];
  computeLineOffsets(entry);

  uint32_t offset = loc.getOffset();
  // Binary search for the line containing this offset.
  auto it = std::upper_bound(entry.lineOffsets.begin(),
                              entry.lineOffsets.end(), offset);
  unsigned line = static_cast<unsigned>(it - entry.lineOffsets.begin());
  unsigned col = offset - entry.lineOffsets[line - 1] + 1;
  return {line, col};
}

llvm::StringRef SourceManager::getSourceLine(SourceLocation loc) const {
  assert(loc.isValid());
  FileID fid = loc.getFileID();
  const auto &entry = files[fid.getID()];
  computeLineOffsets(entry);

  uint32_t offset = loc.getOffset();
  auto it = std::upper_bound(entry.lineOffsets.begin(),
                              entry.lineOffsets.end(), offset);
  unsigned lineIdx = static_cast<unsigned>(it - entry.lineOffsets.begin()) - 1;
  uint32_t lineStart = entry.lineOffsets[lineIdx];
  uint32_t lineEnd;
  if (lineIdx + 1 < entry.lineOffsets.size())
    lineEnd = entry.lineOffsets[lineIdx + 1];
  else
    lineEnd = static_cast<uint32_t>(entry.content.size());
  // Trim trailing newline.
  if (lineEnd > lineStart && entry.content[lineEnd - 1] == '\n')
    --lineEnd;
  if (lineEnd > lineStart && entry.content[lineEnd - 1] == '\r')
    --lineEnd;
  return llvm::StringRef(entry.content.data() + lineStart,
                          lineEnd - lineStart);
}

} // namespace asc
