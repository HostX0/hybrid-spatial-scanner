#include "hss/revisioned_command.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace hss::command {
namespace {

constexpr std::array<uint32_t, 64U> kSha256RoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

bool lowercase_hex(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'f');
}

bool valid_uuid(std::string_view id) {
  if (id.size() != 36U)
    return false;
  bool nonzero = false;
  for (size_t index = 0U; index < id.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (id[index] != '-')
        return false;
      continue;
    }
    if (!lowercase_hex(id[index]))
      return false;
    nonzero = nonzero || id[index] != '0';
  }
  const bool supported_version = id[14] >= '1' && id[14] <= '8';
  const bool rfc_variant = id[19] == '8' || id[19] == '9' ||
                           id[19] == 'a' || id[19] == 'b';
  return nonzero && supported_version && rfc_variant;
}

bool parse_uint64(std::string_view text, uint64_t *value) {
  if (text.empty() || (text.size() > 1U && text.front() == '0') ||
      text.size() > 20U)
    return false;
  uint64_t parsed = 0U;
  for (const char character : text) {
    if (character < '0' || character > '9')
      return false;
    const uint64_t digit = static_cast<uint64_t>(character - '0');
    if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10U)
      return false;
    parsed = parsed * 10U + digit;
  }
  *value = parsed;
  return true;
}

bool valid_utf8(std::string_view bytes) {
  size_t index = 0U;
  while (index < bytes.size()) {
    const uint8_t first = static_cast<uint8_t>(bytes[index]);
    if (first <= 0x7fU) {
      ++index;
      continue;
    }
    size_t continuation_count = 0U;
    uint32_t code_point = 0U;
    uint32_t minimum = 0U;
    if (first >= 0xc2U && first <= 0xdfU) {
      continuation_count = 1U;
      code_point = first & 0x1fU;
      minimum = 0x80U;
    } else if (first >= 0xe0U && first <= 0xefU) {
      continuation_count = 2U;
      code_point = first & 0x0fU;
      minimum = 0x800U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
      continuation_count = 3U;
      code_point = first & 0x07U;
      minimum = 0x10000U;
    } else {
      return false;
    }
    if (continuation_count > bytes.size() - index - 1U)
      return false;
    for (size_t offset = 1U; offset <= continuation_count; ++offset) {
      const uint8_t continuation =
          static_cast<uint8_t>(bytes[index + offset]);
      if ((continuation & 0xc0U) != 0x80U)
        return false;
      code_point = (code_point << 6U) | (continuation & 0x3fU);
    }
    if (code_point < minimum || code_point > 0x10ffffU ||
        (code_point >= 0xd800U && code_point <= 0xdfffU))
      return false;
    index += continuation_count + 1U;
  }
  return true;
}

bool valid_options(const Options &options) {
  return options.max_command_bytes > 0U &&
         options.max_command_bytes <= kHardMaxCommandBytes &&
         options.max_result_bytes > 0U &&
         options.max_result_bytes <= kHardMaxResultBytes &&
         options.max_records > 0U && options.max_records <= kHardMaxRecords &&
         options.max_targets <= kHardMaxTargets;
}

bool valid_scene(const SceneState &scene, size_t max_targets) {
  if (!valid_uuid(scene.project_id) ||
      scene.current_targets.size() > max_targets)
    return false;
  std::unordered_set<std::string_view> ids;
  ids.reserve(scene.current_targets.size());
  for (const CurrentTarget &target : scene.current_targets) {
    if (!valid_uuid(target.id) || target.lineage_revision > scene.revision ||
        !ids.insert(target.id).second)
      return false;
  }
  return true;
}

const CurrentTarget *find_target(const SceneState &scene,
                                 std::string_view id) {
  const auto target =
      std::find_if(scene.current_targets.begin(), scene.current_targets.end(),
                   [id](const CurrentTarget &candidate) {
                     return candidate.id == id;
                   });
  return target == scene.current_targets.end() ? nullptr : &*target;
}

SubmissionResult failure(Status status) { return {.status = status}; }

uint32_t load_be32(const uint8_t *bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24U) |
         (static_cast<uint32_t>(bytes[1]) << 16U) |
         (static_cast<uint32_t>(bytes[2]) << 8U) |
         static_cast<uint32_t>(bytes[3]);
}

void store_be32(uint32_t value, uint8_t *bytes) {
  bytes[0] = static_cast<uint8_t>(value >> 24U);
  bytes[1] = static_cast<uint8_t>(value >> 16U);
  bytes[2] = static_cast<uint8_t>(value >> 8U);
  bytes[3] = static_cast<uint8_t>(value);
}

void process_sha256_block(const uint8_t *block,
                          std::array<uint32_t, 8U> *state) {
  std::array<uint32_t, 64U> words{};
  for (size_t index = 0U; index < 16U; ++index)
    words[index] = load_be32(block + index * 4U);
  for (size_t index = 16U; index < words.size(); ++index) {
    const uint32_t s0 = std::rotr(words[index - 15U], 7) ^
                        std::rotr(words[index - 15U], 18) ^
                        (words[index - 15U] >> 3U);
    const uint32_t s1 = std::rotr(words[index - 2U], 17) ^
                        std::rotr(words[index - 2U], 19) ^
                        (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
  }

  uint32_t a = (*state)[0];
  uint32_t b = (*state)[1];
  uint32_t c = (*state)[2];
  uint32_t d = (*state)[3];
  uint32_t e = (*state)[4];
  uint32_t f = (*state)[5];
  uint32_t g = (*state)[6];
  uint32_t h = (*state)[7];
  for (size_t index = 0U; index < words.size(); ++index) {
    const uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^
                          std::rotr(e, 25);
    const uint32_t choice = (e & f) ^ ((~e) & g);
    const uint32_t temporary1 =
        h + sum1 + choice + kSha256RoundConstants[index] + words[index];
    const uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^
                          std::rotr(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temporary2 = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }
  (*state)[0] += a;
  (*state)[1] += b;
  (*state)[2] += c;
  (*state)[3] += d;
  (*state)[4] += e;
  (*state)[5] += f;
  (*state)[6] += g;
  (*state)[7] += h;
}

} // namespace

RevisionedCommandGate::RevisionedCommandGate(Options options)
    : options_(options) {
  if (valid_options(options_))
    records_.reserve(std::min<size_t>(options_.max_records, 1'024U));
}

const Record *
RevisionedCommandGate::find_record(std::string_view command_id) const {
  const auto found = std::find_if(
      records_.begin(), records_.end(), [command_id](const Storage &entry) {
        return entry.command_id == command_id;
      });
  return found == records_.end() ? nullptr : &found->record;
}

size_t RevisionedCommandGate::record_count() const { return records_.size(); }

SubmissionResult RevisionedCommandGate::submit(
    const CommandEnvelope &command, std::string_view exact_utf8_bytes,
    SceneState &accepted_scene, const ApplyOperation &apply) {
  if (!valid_options(options_))
    return failure(Status::kInvalidOptions);
  if (!valid_uuid(command.command_id) || exact_utf8_bytes.empty() ||
      !valid_utf8(exact_utf8_bytes))
    return failure(Status::kInvalidCommand);
  if (exact_utf8_bytes.size() > options_.max_command_bytes)
    return failure(Status::kResourceLimitExceeded);

  const std::array<uint8_t, 32U> digest = sha256(exact_utf8_bytes);
  if (const Record *stored = find_record(command.command_id)) {
    if (stored->exact_command_utf8_bytes != exact_utf8_bytes) {
      SubmissionResult result = failure(Status::kDuplicateMismatch);
      result.command_sha256 = digest;
      return result;
    }
    return {.status = Status::kStoredRetry,
            .outcome = stored->outcome,
            .result_utf8_bytes = stored->exact_result_utf8_bytes,
            .command_sha256 = stored->command_sha256,
            .durably_committed = false};
  }

  uint64_t expected_revision = 0U;
  uint64_t event_sequence = 0U;
  if (!valid_uuid(command.project_id) ||
      !parse_uint64(command.expected_scene_revision, &expected_revision) ||
      !parse_uint64(command.event_sequence, &event_sequence) ||
      command.target_ids.size() > options_.max_targets ||
      !valid_scene(accepted_scene, options_.max_targets))
    return failure(Status::kInvalidCommand);
  (void)event_sequence;
  if (command.project_id != accepted_scene.project_id)
    return failure(Status::kProjectMismatch);

  std::unordered_set<std::string_view> target_ids;
  target_ids.reserve(command.target_ids.size());
  for (const std::string &target_id : command.target_ids) {
    if (!valid_uuid(target_id) || !target_ids.insert(target_id).second)
      return failure(Status::kInvalidCommand);
  }

  if (expected_revision > accepted_scene.revision)
    return failure(Status::kFutureRevision);
  const bool stale = expected_revision < accepted_scene.revision;
  if (stale && !command.allow_stale_unchanged_lineage)
    return failure(Status::kStaleRevision);
  if (stale && command.target_ids.empty())
    return failure(Status::kTargetLineageChanged);

  for (const std::string &target_id : command.target_ids) {
    const CurrentTarget *target = find_target(accepted_scene, target_id);
    if (target == nullptr) {
      return failure(stale ? Status::kTargetLineageChanged
                           : Status::kTargetNotCurrent);
    }
    if (stale && target->lineage_revision > expected_revision)
      return failure(Status::kTargetLineageChanged);
  }

  if (records_.size() >= options_.max_records)
    return failure(Status::kResourceLimitExceeded);
  if (!apply)
    return failure(Status::kApplyFailure);

  SceneState candidate = accepted_scene;
  ApplyResult applied;
  try {
    applied = apply(command, candidate);
  } catch (const std::exception &) {
    return failure(Status::kApplyFailure);
  } catch (...) {
    return failure(Status::kApplyFailure);
  }

  if (applied.result_utf8_bytes.empty() ||
      !valid_utf8(applied.result_utf8_bytes))
    return failure(Status::kInvalidApplyResult);
  if (applied.result_utf8_bytes.size() > options_.max_result_bytes)
    return failure(Status::kResourceLimitExceeded);

  uint64_t associated_revision = accepted_scene.revision;
  Status status = Status::kRejected;
  if (applied.outcome == Outcome::kApplied) {
    if (accepted_scene.revision == std::numeric_limits<uint64_t>::max() ||
        candidate.project_id != accepted_scene.project_id ||
        candidate.revision != accepted_scene.revision + 1U ||
        !valid_scene(candidate, options_.max_targets))
      return failure(Status::kInvalidApplyResult);
    accepted_scene = std::move(candidate);
    associated_revision = accepted_scene.revision;
    status = Status::kApplied;
  } else if (applied.outcome == Outcome::kConflict) {
    status = Status::kConflict;
  }

  Record record{std::string(exact_utf8_bytes), digest,
                std::move(applied.result_utf8_bytes), applied.outcome,
                associated_revision};
  const std::string result_copy = record.exact_result_utf8_bytes;
  records_.push_back({command.command_id, std::move(record)});
  return {.status = status,
          .outcome = applied.outcome,
          .result_utf8_bytes = result_copy,
          .command_sha256 = digest,
          .durably_committed = false};
}

const char *status_name(Status status) {
  switch (status) {
  case Status::kApplied:
    return "applied";
  case Status::kStoredRetry:
    return "stored_retry";
  case Status::kConflict:
    return "conflict";
  case Status::kRejected:
    return "rejected";
  case Status::kInvalidOptions:
    return "invalid_options";
  case Status::kInvalidCommand:
    return "invalid_command";
  case Status::kResourceLimitExceeded:
    return "resource_limit_exceeded";
  case Status::kDuplicateMismatch:
    return "duplicate_mismatch";
  case Status::kProjectMismatch:
    return "project_mismatch";
  case Status::kStaleRevision:
    return "stale_revision";
  case Status::kFutureRevision:
    return "future_revision";
  case Status::kTargetNotCurrent:
    return "target_not_current";
  case Status::kTargetLineageChanged:
    return "target_lineage_changed";
  case Status::kApplyFailure:
    return "apply_failure";
  case Status::kInvalidApplyResult:
    return "invalid_apply_result";
  }
  return "unknown";
}

std::array<uint8_t, 32U> sha256(std::string_view bytes) {
  std::array<uint32_t, 8U> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                 0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                 0x1f83d9abU, 0x5be0cd19U};
  const auto *data = reinterpret_cast<const uint8_t *>(bytes.data());
  size_t offset = 0U;
  while (bytes.size() - offset >= 64U) {
    process_sha256_block(data + offset, &state);
    offset += 64U;
  }

  std::array<uint8_t, 128U> tail{};
  const size_t remaining = bytes.size() - offset;
  if (remaining > 0U)
    std::copy_n(data + offset, remaining, tail.begin());
  tail[remaining] = 0x80U;
  const size_t padded_size = remaining < 56U ? 64U : 128U;
  const uint64_t bit_length = static_cast<uint64_t>(bytes.size()) * 8U;
  for (size_t index = 0U; index < 8U; ++index) {
    tail[padded_size - 1U - index] =
        static_cast<uint8_t>(bit_length >> (index * 8U));
  }
  process_sha256_block(tail.data(), &state);
  if (padded_size == 128U)
    process_sha256_block(tail.data() + 64U, &state);

  std::array<uint8_t, 32U> digest{};
  for (size_t index = 0U; index < state.size(); ++index)
    store_be32(state[index], digest.data() + index * 4U);
  return digest;
}

std::string sha256_hex(const std::array<uint8_t, 32U> &digest) {
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string result(digest.size() * 2U, '0');
  for (size_t index = 0U; index < digest.size(); ++index) {
    result[index * 2U] = kHex[digest[index] >> 4U];
    result[index * 2U + 1U] = kHex[digest[index] & 0x0fU];
  }
  return result;
}

} // namespace hss::command
