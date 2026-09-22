#pragma once

#include "hss/revisioned_command.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hss::scene_editing {

inline constexpr size_t kDefaultMaxSceneBytes = 16U * 1024U * 1024U;
inline constexpr size_t kDefaultMaxEntities = 65'536U;
inline constexpr size_t kMaxSolverScalars = 256U;

enum class Status : uint8_t {
  kApplied,
  kStoredRetry,
  kConflict,
  kRejected,
  kPendingUnsupported,
  kInvalidScene,
  kInvalidCommand,
  kDuplicateMismatch,
  kProjectMismatch,
  kStaleRevision,
  kFutureRevision,
  kTargetNotCurrent,
  kTargetLineageChanged,
  kResourceLimitExceeded,
  kInternalError,
};

struct Options {
  size_t max_scene_bytes{kDefaultMaxSceneBytes};
  size_t max_command_bytes{256U * 1024U};
  size_t max_result_bytes{256U * 1024U};
  size_t max_records{65'536U};
  size_t max_entities{kDefaultMaxEntities};
  size_t max_solver_scalars{kMaxSolverScalars};
  uint32_t max_solver_iterations{40U};
  double feasibility_tolerance{1e-7};
  double geometry_epsilon_m{1e-8};
  double maximum_coordinate_m{1'000'000.0};
};

// This record is an in-memory semantic transaction. The storage adapter writes
// exact_command_utf8 and exact_result_utf8 with the ordered event context,
// before/after revisions and its own framing/checksum. before/after scene bytes
// are retained here so undo can append a compensating operation without
// altering source observations or deleting the original record.
struct JournalRecord {
  uint64_t ordinal{0U};
  std::string command_id;
  std::string command_type;
  std::string original_command_id;
  std::string event_sequence;
  std::string exact_command_utf8;
  std::array<uint8_t, 32U> command_sha256{};
  std::string exact_result_utf8;
  uint64_t before_scene_revision{0U};
  uint64_t after_scene_revision{0U};
  std::string before_scene_json;
  std::string after_scene_json;
};

struct DynamicExclusion {
  std::string command_id;
  std::string project_id;
  std::array<double, 3U> min_m{};
  std::array<double, 3U> max_m{};
  uint64_t start_sequence{0U};
  uint64_t end_sequence{0U};
};

struct ProcessResult {
  Status status{Status::kInvalidCommand};
  bool scene_changed{false};

  // Populated for accepted mutations. Rejections preserve the caller's scene.
  std::string candidate_scene_json;

  // Schema-valid command-result JSON is returned whenever a valid command_id
  // could be read. Byte-identical retries return the stored bytes exactly.
  std::string exact_result_json;
  std::array<uint8_t, 32U> command_sha256{};
  std::optional<JournalRecord> journal_record;
  std::optional<DynamicExclusion> pending_dynamic_exclusion;
};

class Processor {
public:
  explicit Processor(Options options = {});

  // The owner serializes calls. current_scene_json remains immutable; accepted
  // mutations are returned as a complete candidate and can be atomically
  // installed by the engine. Raw captured mesh buffers are outside this API.
  [[nodiscard]] ProcessResult
  process(std::string_view current_scene_json,
          std::string_view exact_command_utf8);

  [[nodiscard]] const std::vector<JournalRecord> &journal() const;
  [[nodiscard]] const JournalRecord *
  find_journal_record(std::string_view command_id) const;
  [[nodiscard]] const std::vector<DynamicExclusion> &
  pending_dynamic_exclusions() const;

private:
  Options options_;
  command::RevisionedCommandGate gate_;
  std::vector<JournalRecord> journal_;
  std::vector<DynamicExclusion> dynamic_exclusions_;
};

[[nodiscard]] const char *status_name(Status status);

// Independent derivative oracle used by focused tests. Returns false for
// nonfinite/coincident endpoints. analytic receives d(||a-b||-L)/d(a,b).
[[nodiscard]] bool distance_constraint_derivative(
    const std::array<double, 3U> &a, const std::array<double, 3U> &b,
    std::array<double, 6U> *analytic);

} // namespace hss::scene_editing
