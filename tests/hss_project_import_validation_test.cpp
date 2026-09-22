#include "hss/project_import_validation.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using hss::project_import::ArchiveEntry;
using hss::project_import::ContentKind;
using hss::project_import::EntryKind;
using hss::project_import::Limits;
using hss::project_import::ReadResult;
using hss::project_import::ReadState;
using hss::project_import::Sha256;
using hss::project_import::Status;
using hss::project_import::StreamingReader;
using hss::project_import::ValidationResult;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

Sha256 sha256(std::initializer_list<uint8_t> bytes) {
  // SHA-256 test vectors for the two byte sequences used below. Keeping the
  // expected digests static proves the portable streaming implementation,
  // rather than deriving its expected output with the code under test.
  if (bytes.size() == 3U && std::equal(bytes.begin(), bytes.end(), std::array<uint8_t, 3>{'a', 'b', 'c'}.begin())) {
    return {0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU, 0x41U, 0x41U, 0x40U,
            0xdeU, 0x5dU, 0xaeU, 0x22U, 0x23U, 0xb0U, 0x03U, 0x61U, 0xa3U, 0x96U, 0x17U,
            0x7aU, 0x9cU, 0xb4U, 0x10U, 0xffU, 0x61U, 0xf2U, 0x00U, 0x15U, 0xadU};
  }
  return {0x2cU, 0xf2U, 0x4dU, 0xbaU, 0x5fU, 0xb0U, 0xa3U, 0x0eU, 0x26U, 0xe8U, 0x3bU,
          0x2aU, 0xc5U, 0xb9U, 0xe2U, 0x9eU, 0x1bU, 0x16U, 0x1eU, 0x5cU, 0x1fU, 0xa7U,
          0x42U, 0x5eU, 0x73U, 0x04U, 0x33U, 0x62U, 0x93U, 0x8bU, 0x98U, 0x24U};
}

class VectorReader final : public StreamingReader {
 public:
  explicit VectorReader(std::vector<uint8_t> bytes, bool fail = false)
      : bytes_(std::move(bytes)), fail_(fail) {}

  ReadResult read(uint8_t* destination, size_t capacity) override {
    ++calls;
    if (fail_) return {ReadState::kFailure, 0U};
    if (position_ == bytes_.size()) return {ReadState::kEnd, 0U};
    const size_t copied = std::min(capacity, bytes_.size() - position_);
    std::copy_n(bytes_.data() + position_, copied, destination);
    position_ += copied;
    return {ReadState::kData, copied};
  }

  uint32_t calls{0};

 private:
  std::vector<uint8_t> bytes_;
  size_t position_{0};
  bool fail_{false};
};

ArchiveEntry file(const char* path, VectorReader* reader, uint64_t declared, Sha256 digest) {
  ArchiveEntry entry{};
  entry.path = path;
  entry.kind = EntryKind::kRegularFile;
  entry.content_kind = ContentKind::kBinary;
  entry.declared_uncompressed_bytes = declared;
  entry.has_sha256 = true;
  entry.sha256 = digest;
  entry.reader = reader;
  return entry;
}

void expect_status(const std::vector<ArchiveEntry>& entries, const Limits& limits, Status expected,
                   const char* message) {
  const ValidationResult result = hss::project_import::validate_archive(entries, limits);
  check(result.status == expected, message);
  check(result.validated_paths.empty(), "failure must never publish a partial path list");
}

}  // namespace

int main() {
  const Sha256 abc = sha256({'a', 'b', 'c'});
  const Sha256 hello = sha256({'h', 'e', 'l', 'l', 'o'});
  Limits limits{};

  VectorReader good_reader({'a', 'b', 'c'});
  ValidationResult good = hss::project_import::validate_archive({file("chunks/source.bin", &good_reader, 3U, abc)}, limits);
  check(good.status == Status::kOk && good.validated_paths == std::vector<std::string>{"chunks/source.bin"},
        "canonical regular file validates after its declared stream length and SHA-256 match");

  for (const char* path : {"/absolute.bin", "./dot.bin", "folder/../dot.bin", "folder//empty.bin", "folder\\backslash.bin", "C:drive.bin"}) {
    VectorReader reader({'a', 'b', 'c'});
    expect_status({file(path, &reader, 3U, abc)}, limits, Status::kInvalidPath,
                  "hostile archive path is rejected before reader use");
    check(reader.calls == 0U, "invalid path never reaches the streaming reader");
  }
  std::string nul_path("chunks/nul");
  nul_path.push_back('\0');
  nul_path += ".bin";
  VectorReader nul_reader({'a', 'b', 'c'});
  ArchiveEntry nul = file("placeholder", &nul_reader, 3U, abc);
  nul.path = nul_path;
  expect_status({nul}, limits, Status::kInvalidPath, "NUL archive path is rejected");

  VectorReader first_duplicate({'a', 'b', 'c'});
  VectorReader normalized_duplicate({'a', 'b', 'c'});
  expect_status({file("chunks/a.bin", &first_duplicate, 3U, abc),
                 file("chunks/./a.bin", &normalized_duplicate, 3U, abc)},
                limits, Status::kDuplicatePath, "normalization collision is rejected as a duplicate");
  check(first_duplicate.calls == 0U && normalized_duplicate.calls == 0U,
        "duplicate metadata is rejected before any entry stream is consumed");

  for (EntryKind link : {EntryKind::kSymbolicLink, EntryKind::kHardLink}) {
    VectorReader reader({'a', 'b', 'c'});
    ArchiveEntry entry = file("chunks/link", &reader, 3U, abc);
    entry.kind = link;
    expect_status({entry}, limits, Status::kUnsupportedEntry, "archive links are unsupported at the baseline boundary");
    check(reader.calls == 0U, "links cannot reach a reader or extraction layer");
  }

  Limits one_file = limits;
  one_file.max_file_count = 1U;
  VectorReader count_a({'a', 'b', 'c'});
  VectorReader count_b({'a', 'b', 'c'});
  expect_status({file("chunks/a", &count_a, 3U, abc), file("chunks/b", &count_b, 3U, abc)}, one_file,
                Status::kResourceLimit, "file-count limit rejects before streaming");
  check(count_a.calls == 0U && count_b.calls == 0U, "count limit prevents stream allocation/consumption");

  Limits small_expansion = limits;
  small_expansion.max_expanded_bytes = 4U;
  VectorReader bomb({'a', 'b', 'c'});
  expect_status({file("chunks/bomb", &bomb, 5U, hello)}, small_expansion, Status::kResourceLimit,
                "zip-bomb-style declared expansion is rejected before reading");
  check(bomb.calls == 0U, "declared expanded-byte limit prevents reader use");

  Limits small_entry = limits;
  small_entry.max_binary_entry_bytes = 2U;
  VectorReader oversized({'a', 'b', 'c'});
  expect_status({file("chunks/oversized", &oversized, 3U, abc)}, small_entry, Status::kResourceLimit,
                "per-entry limit rejects before allocation or reader use");
  check(oversized.calls == 0U, "per-entry limit is pre-stream");

  Limits small_json = limits;
  small_json.max_json_entry_bytes = 2U;
  VectorReader json_oversized({'a', 'b', 'c'});
  ArchiveEntry json = file("events/command.json", &json_oversized, 3U, abc);
  json.content_kind = ContentKind::kJson;
  expect_status({json}, small_json, Status::kResourceLimit,
                "JSON-record limit rejects before allocation or reader use");
  check(json_oversized.calls == 0U, "JSON limit is pre-stream");

  VectorReader short_read({'a', 'b'});
  expect_status({file("chunks/short", &short_read, 3U, abc)}, limits, Status::kLengthMismatch,
                "short stream fails its declared length");
  VectorReader bad_hash({'a', 'b', 'c'});
  expect_status({file("chunks/hash", &bad_hash, 3U, hello)}, limits, Status::kChecksumMismatch,
                "stream SHA-256 mismatch is explicit");
  VectorReader long_read({'a', 'b', 'c', 'd'});
  expect_status({file("chunks/long", &long_read, 3U, abc)}, limits, Status::kLengthMismatch,
                "trailing stream bytes violate the declared length");

  VectorReader first_good({'a', 'b', 'c'});
  VectorReader later_bad({'a', 'b', 'c'});
  expect_status({file("chunks/first", &first_good, 3U, abc), file("chunks/second", &later_bad, 3U, hello)},
                limits, Status::kChecksumMismatch,
                "a later stream failure cannot publish already validated earlier paths");

  std::cout << "project import hostile-path, limits, streaming hash, and atomic publication checks passed\n";
  return EXIT_SUCCESS;
}
