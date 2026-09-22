#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hss::command {

inline constexpr size_t kHardMaxCommandBytes = 4U * 1024U * 1024U;
inline constexpr size_t kHardMaxResultBytes = 4U * 1024U * 1024U;
inline constexpr size_t kHardMaxRecords = 1'000'000U;
inline constexpr size_t kHardMaxTargets = 4'096U;

// This is parsed metadata for exact_utf8_bytes. The caller must derive it from
// those same bytes after syntax/schema/variant validation. This module does not
// parse or reserialize JSON, so byte identity remains the retry authority.
struct CommandEnvelope {
  std::string command_id;
  std::string project_id;
  std::string expected_scene_revision;
  std::string event_sequence;
  std::vector<std::string> target_ids;

  // A stale command is never reconciled by default. Set this only when the
  // command variant's contract explicitly permits unchanged-lineage replay.
  bool allow_stale_unchanged_lineage{false};
};

// The target ID is current in this scene. lineage_revision is the most recent
// accepted scene revision at which its identity/lineage changed. A stale
// command expecting an earlier revision cannot prove unchanged lineage.
struct CurrentTarget {
  std::string id;
  uint64_t lineage_revision{0U};
  bool operator==(const CurrentTarget &) const = default;
};

struct SceneState {
  std::string project_id;
  uint64_t revision{0U};
  std::vector<CurrentTarget> current_targets;
  bool operator==(const SceneState &) const = default;
};

enum class Outcome : uint8_t {
  kApplied,
  kConflict,
  kRejected,
};

// result_utf8_bytes are retained and returned byte-for-byte on a retry. They
// remain opaque to this module; no JSON serialization or canonicalization is
// performed here.
struct ApplyResult {
  Outcome outcome{Outcome::kRejected};
  std::string result_utf8_bytes;
};

enum class Status : uint8_t {
  kApplied,
  kStoredRetry,
  kConflict,
  kRejected,
  kInvalidOptions,
  kInvalidCommand,
  kResourceLimitExceeded,
  kDuplicateMismatch,
  kProjectMismatch,
  kStaleRevision,
  kFutureRevision,
  kTargetNotCurrent,
  kTargetLineageChanged,
  kApplyFailure,
  kInvalidApplyResult,
};

struct SubmissionResult {
  Status status{Status::kInvalidCommand};
  std::optional<Outcome> outcome;
  std::string result_utf8_bytes;
  std::array<uint8_t, 32U> command_sha256{};

  // This standalone gate owns only process memory. It never claims journal,
  // manifest, filesystem, or other durable-save completion.
  bool durably_committed{false};
};

struct Record {
  std::string exact_command_utf8_bytes;
  std::array<uint8_t, 32U> command_sha256{};
  std::string exact_result_utf8_bytes;
  Outcome outcome{Outcome::kRejected};
  uint64_t associated_scene_revision{0U};
};

struct Options {
  size_t max_command_bytes{256U * 1024U};
  size_t max_result_bytes{256U * 1024U};
  size_t max_records{65'536U};
  size_t max_targets{64U};
};

using ApplyOperation =
    std::function<ApplyResult(const CommandEnvelope &, SceneState &candidate)>;

class RevisionedCommandGate {
public:
  explicit RevisionedCommandGate(Options options = {});

  // Validation and application are atomic with respect to accepted_scene. The
  // callback receives a copy; stale/conflicting/rejected/invalid candidates do
  // not replace the caller's prior accepted state. This object is deliberately
  // not internally synchronized; its owner must serialize submissions.
  [[nodiscard]] SubmissionResult
  submit(const CommandEnvelope &command, std::string_view exact_utf8_bytes,
         SceneState &accepted_scene, const ApplyOperation &apply);

  [[nodiscard]] const Record *find_record(std::string_view command_id) const;
  [[nodiscard]] size_t record_count() const;

private:
  Options options_;
  struct Storage {
    std::string command_id;
    Record record;
  };
  std::vector<Storage> records_;
};

[[nodiscard]] const char *status_name(Status status);
[[nodiscard]] std::array<uint8_t, 32U> sha256(std::string_view bytes);
[[nodiscard]] std::string sha256_hex(const std::array<uint8_t, 32U> &digest);

} // namespace hss::command
