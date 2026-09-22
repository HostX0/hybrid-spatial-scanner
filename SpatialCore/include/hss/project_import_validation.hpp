#ifndef HSS_PROJECT_IMPORT_VALIDATION_HPP
#define HSS_PROJECT_IMPORT_VALIDATION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hss::project_import {

// This module is the archive boundary, not an archive extractor.  A platform
// adapter supplies already-enumerated entries and a streaming reader for each
// regular file.  No path is extracted or published by this portable layer.
using Sha256 = std::array<uint8_t, 32>;

enum class Status : uint8_t {
  kOk,
  kInvalidArgument,
  kInvalidPath,
  kDuplicatePath,
  kUnsupportedEntry,
  kResourceLimit,
  kLengthMismatch,
  kChecksumMismatch,
  kReaderFailure,
  kInternalError,
};

enum class EntryKind : uint8_t { kRegularFile, kDirectory, kSymbolicLink, kHardLink };
enum class ContentKind : uint8_t { kBinary, kJson, kOther };
enum class ReadState : uint8_t { kData, kEnd, kFailure };

struct ReadResult {
  ReadState state{ReadState::kEnd};
  size_t bytes{0};
};

// Implementations return at most capacity bytes. kData must return at least
// one byte; kEnd and kFailure must return zero. The caller owns the reader and
// keeps it alive for validate_archive().
class StreamingReader {
 public:
  virtual ~StreamingReader() = default;
  virtual ReadResult read(uint8_t* destination, size_t capacity) = 0;
};

struct ArchiveEntry {
  std::string path;
  EntryKind kind{EntryKind::kRegularFile};
  ContentKind content_kind{ContentKind::kOther};
  uint64_t declared_uncompressed_bytes{0};
  bool has_sha256{false};
  Sha256 sha256{};
  StreamingReader* reader{nullptr};
};

struct Limits {
  uint64_t max_file_count{20000U};
  uint64_t max_expanded_bytes{8ULL * 1024ULL * 1024ULL * 1024ULL};
  uint64_t max_binary_entry_bytes{256ULL * 1024ULL * 1024ULL};
  uint64_t max_json_entry_bytes{16ULL * 1024ULL * 1024ULL};
  uint64_t max_other_entry_bytes{256ULL * 1024ULL * 1024ULL};
  uint64_t max_path_bytes{4096U};
};

struct ValidationResult {
  Status status{Status::kOk};
  // Populated only after the full archive, including every stream hash and
  // length, validates. A failure always returns an empty vector.
  std::vector<std::string> validated_paths;
};

// Validates the complete archive before returning any accepted path. This is a
// preparation gate for an extractor/committer; it performs neither operation.
ValidationResult validate_archive(const std::vector<ArchiveEntry>& entries, const Limits& limits);

}  // namespace hss::project_import

#endif
