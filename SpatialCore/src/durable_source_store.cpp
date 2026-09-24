#include "hss/durable_source_store.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

#include <fcntl.h>
#include <unistd.h>

namespace hss::storage {
namespace {

constexpr char kActiveManifest[] = "active.manifest";
constexpr char kPreviousManifest[] = "previous.manifest";
constexpr char kManifestTemporary[] = "temporary/manifest.pending";
constexpr char kPreviousTemporary[] = "temporary/previous.pending";
constexpr char kProjectMetadata[] = "project.json";
constexpr char kProjectMetadataTemporary[] = "temporary/project.pending";
constexpr uint32_t kManifestFormatVersion = 1;
constexpr uint64_t kMaximumProjectBytes = 8ULL * 1024U * 1024U * 1024U;
constexpr uint64_t kMaximumChunkBytes = 256U * 1024U * 1024U;
constexpr uint64_t kMaximumFileCount = 20'000U;
constexpr uint64_t kMaximumManifestBytes = 16U * 1024U * 1024U;

using Digest = std::array<uint8_t, 32>;

class Sha256 {
public:
  Sha256() { reset(); }

  void update(const uint8_t *data, size_t size) {
    for (size_t index = 0; index < size; ++index) {
      buffer_[buffer_size_++] = data[index];
      if (buffer_size_ == buffer_.size()) {
        transform(buffer_.data());
        bit_length_ += 512U;
        buffer_size_ = 0;
      }
    }
  }

  Digest finish() {
    const uint64_t final_bits =
        bit_length_ + static_cast<uint64_t>(buffer_size_) * 8U;
    buffer_[buffer_size_++] = 0x80U;
    if (buffer_size_ > 56U) {
      while (buffer_size_ < 64U) {
        buffer_[buffer_size_++] = 0U;
      }
      transform(buffer_.data());
      buffer_size_ = 0;
    }
    while (buffer_size_ < 56U) {
      buffer_[buffer_size_++] = 0U;
    }
    for (size_t index = 0; index < 8U; ++index) {
      buffer_[63U - index] = static_cast<uint8_t>(final_bits >> (8U * index));
    }
    transform(buffer_.data());

    Digest digest{};
    for (size_t index = 0; index < state_.size(); ++index) {
      digest[index * 4U] = static_cast<uint8_t>(state_[index] >> 24U);
      digest[index * 4U + 1U] = static_cast<uint8_t>(state_[index] >> 16U);
      digest[index * 4U + 2U] = static_cast<uint8_t>(state_[index] >> 8U);
      digest[index * 4U + 3U] = static_cast<uint8_t>(state_[index]);
    }
    return digest;
  }

private:
  static constexpr std::array<uint32_t, 64> kConstants = {
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

  static uint32_t rotate_right(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32U - count));
  }

  void reset() {
    state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
              0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    buffer_size_ = 0;
    bit_length_ = 0;
  }

  void transform(const uint8_t *block) {
    std::array<uint32_t, 64> words{};
    for (size_t index = 0; index < 16U; ++index) {
      words[index] = (static_cast<uint32_t>(block[index * 4U]) << 24U) |
                     (static_cast<uint32_t>(block[index * 4U + 1U]) << 16U) |
                     (static_cast<uint32_t>(block[index * 4U + 2U]) << 8U) |
                     static_cast<uint32_t>(block[index * 4U + 3U]);
    }
    for (size_t index = 16U; index < words.size(); ++index) {
      const uint32_t s0 = rotate_right(words[index - 15U], 7U) ^
                          rotate_right(words[index - 15U], 18U) ^
                          (words[index - 15U] >> 3U);
      const uint32_t s1 = rotate_right(words[index - 2U], 17U) ^
                          rotate_right(words[index - 2U], 19U) ^
                          (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];
    for (size_t index = 0; index < words.size(); ++index) {
      const uint32_t s1 =
          rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
      const uint32_t choice = (e & f) ^ ((~e) & g);
      const uint32_t temp1 = h + s1 + choice + kConstants[index] + words[index];
      const uint32_t s0 =
          rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<uint32_t, 8> state_{};
  std::array<uint8_t, 64> buffer_{};
  size_t buffer_size_{0};
  uint64_t bit_length_{0};
};

Digest sha256(const std::vector<uint8_t> &bytes) {
  Sha256 hash;
  if (!bytes.empty()) {
    hash.update(bytes.data(), bytes.size());
  }
  return hash.finish();
}

std::string hex_encode(const uint8_t *data, size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(size * 2U);
  for (size_t index = 0; index < size; ++index) {
    result.push_back(kHex[data[index] >> 4U]);
    result.push_back(kHex[data[index] & 0x0fU]);
  }
  return result;
}

std::optional<std::vector<uint8_t>> hex_decode(std::string_view value) {
  if (value.size() % 2U != 0U) {
    return std::nullopt;
  }
  std::vector<uint8_t> decoded(value.size() / 2U);
  const auto nibble = [](char character) -> int {
    if (character >= '0' && character <= '9')
      return character - '0';
    if (character >= 'a' && character <= 'f')
      return character - 'a' + 10;
    return -1;
  };
  for (size_t index = 0; index < decoded.size(); ++index) {
    const int high = nibble(value[index * 2U]);
    const int low = nibble(value[index * 2U + 1U]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    decoded[index] = static_cast<uint8_t>((high << 4U) | low);
  }
  return decoded;
}

bool is_zero(const std::array<uint8_t, 16> &value) {
  return std::all_of(value.begin(), value.end(),
                     [](uint8_t byte) { return byte == 0U; });
}

bool valid_limits(const RetentionLimits &limits) {
  if (limits.max_epoch_records == 0U || limits.max_depth_records == 0U ||
      limits.max_mesh_records == 0U || limits.max_command_records == 0U ||
      limits.max_retained_bytes == 0U || limits.max_chunk_bytes == 0U ||
      limits.max_chunk_bytes > limits.max_retained_bytes ||
      limits.max_retained_bytes > kMaximumProjectBytes ||
      limits.max_chunk_bytes > kMaximumChunkBytes ||
      limits.max_epoch_records > kMaximumFileCount ||
      limits.max_depth_records > kMaximumFileCount ||
      limits.max_mesh_records > kMaximumFileCount ||
      limits.max_command_records > kMaximumFileCount) {
    return false;
  }
  uint64_t total = limits.max_epoch_records;
  if (limits.max_depth_records > kMaximumFileCount - total)
    return false;
  total += limits.max_depth_records;
  if (limits.max_mesh_records > kMaximumFileCount - total)
    return false;
  total += limits.max_mesh_records;
  return limits.max_command_records <= kMaximumFileCount - total;
}

bool valid_record(const SourceRecord &record, const RetentionLimits &limits) {
  const bool control = record.kind == SourceKind::kCommand ||
                       record.kind == SourceKind::kPlanReference;
  if ((record.kind != SourceKind::kDepth && record.kind != SourceKind::kMesh &&
       record.kind != SourceKind::kEpoch &&
       record.kind != SourceKind::kCommand &&
       record.kind != SourceKind::kPlanReference) ||
      record.provenance != (control ? SourceProvenance::kAcceptedControl
                                    : SourceProvenance::kCaptured) ||
      !std::isfinite(record.monotonic_time_s) || record.payload_version == 0U ||
      record.payload.size() > limits.max_chunk_bytes ||
      record.payload.size() >
          static_cast<size_t>(std::numeric_limits<uint64_t>::max())) {
    return false;
  }
  // Selected depth must actually contain copied measurements. A zero-length
  // mesh event remains valid because B-006 remove events have no geometry
  // payload.
  if ((record.kind == SourceKind::kDepth || record.kind == SourceKind::kEpoch ||
       record.kind == SourceKind::kCommand ||
       record.kind == SourceKind::kPlanReference) &&
      record.payload.empty()) {
    return false;
  }
  return record.kind == SourceKind::kMesh ? !is_zero(record.anchor_id)
                                          : is_zero(record.anchor_id);
}

bool canonical_chunk_path(std::string_view path) {
  if (path.size() < 12U || path.rfind("chunks/", 0U) != 0U ||
      path.find("..") != std::string_view::npos ||
      path.find('\\') != std::string_view::npos ||
      path.find('\0') != std::string_view::npos) {
    return false;
  }
  return path.find('/', 7U) == std::string_view::npos;
}

std::string kind_name(SourceKind kind) {
  switch (kind) {
  case SourceKind::kDepth:
    return "depth";
  case SourceKind::kMesh:
    return "mesh";
  case SourceKind::kEpoch:
    return "epoch";
  case SourceKind::kCommand:
    return "command";
  case SourceKind::kPlanReference:
    return "plan-reference";
  }
  return {};
}

std::optional<SourceKind> parse_kind(std::string_view name) {
  if (name == "depth")
    return SourceKind::kDepth;
  if (name == "mesh")
    return SourceKind::kMesh;
  if (name == "epoch")
    return SourceKind::kEpoch;
  if (name == "command")
    return SourceKind::kCommand;
  if (name == "plan-reference")
    return SourceKind::kPlanReference;
  return std::nullopt;
}

std::string provenance_name(SourceProvenance provenance) {
  return provenance == SourceProvenance::kCaptured ? "captured"
                                                   : "accepted-control";
}

std::optional<SourceProvenance> parse_provenance(std::string_view name) {
  if (name == "captured")
    return SourceProvenance::kCaptured;
  if (name == "accepted-control")
    return SourceProvenance::kAcceptedControl;
  return std::nullopt;
}

template <typename T> bool parse_unsigned(std::string_view text, T *value) {
  static_assert(std::is_unsigned_v<T>);
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), *value);
  return error == std::errc{} && end == text.data() + text.size();
}

bool parse_double(std::string_view text, double *value) {
  if (value == nullptr || text.empty() || text.size() > 64U)
    return false;
  std::istringstream stream{std::string(text)};
  stream.imbue(std::locale::classic());
  stream >> std::noskipws >> *value;
  return stream && stream.peek() == std::char_traits<char>::eof() &&
         std::isfinite(*value);
}

bool append_double(std::string *destination, double value) {
  if (destination == nullptr || !std::isfinite(value))
    return false;
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10)
         << value;
  if (!stream)
    return false;
  const std::string encoded = stream.str();
  if (encoded.empty() || encoded.size() > 64U)
    return false;
  destination->append(encoded);
  return true;
}

std::string chunk_path(const StoredRecord &record) {
  const std::string digest =
      hex_encode(record.sha256.data(), record.sha256.size());
  return "chunks/" + kind_name(record.source.kind) + "-e" +
         std::to_string(record.source.session_epoch) + "-s" +
         std::to_string(record.source.event_sequence) + "-" +
         digest.substr(0, 16U) + ".bin";
}

bool less_replay_order(const StoredRecord &left, const StoredRecord &right) {
  if (left.source.event_sequence != right.source.event_sequence) {
    return left.source.event_sequence < right.source.event_sequence;
  }
  if (left.source.kind != right.source.kind) {
    return left.source.kind < right.source.kind;
  }
  return left.relative_path < right.relative_path;
}

bool is_manifest_record_valid(const StoredRecord &record,
                              const RetentionLimits &limits) {
  const bool control = record.source.kind == SourceKind::kCommand ||
                       record.source.kind == SourceKind::kPlanReference;
  if ((record.source.kind != SourceKind::kDepth &&
       record.source.kind != SourceKind::kMesh &&
       record.source.kind != SourceKind::kEpoch &&
       record.source.kind != SourceKind::kCommand &&
       record.source.kind != SourceKind::kPlanReference) ||
      record.source.provenance != (control ? SourceProvenance::kAcceptedControl
                                           : SourceProvenance::kCaptured) ||
      !std::isfinite(record.source.monotonic_time_s) ||
      record.source.payload_version == 0U ||
      record.payload_length > limits.max_chunk_bytes ||
      (record.source.kind == SourceKind::kDepth &&
       record.payload_length == 0U) ||
      (record.source.kind == SourceKind::kEpoch &&
       record.payload_length == 0U) ||
      (record.source.kind == SourceKind::kCommand &&
       record.payload_length == 0U) ||
      (record.source.kind == SourceKind::kPlanReference &&
       record.payload_length == 0U) ||
      (record.source.kind == SourceKind::kMesh &&
       is_zero(record.source.anchor_id)) ||
      (record.source.kind != SourceKind::kMesh &&
       !is_zero(record.source.anchor_id)) ||
      !canonical_chunk_path(record.relative_path) ||
      record.relative_path != chunk_path(record)) {
    return false;
  }
  return true;
}

std::string serialize_manifest(uint64_t revision,
                               const std::vector<StoredRecord> &records) {
  std::string body = "HSS_SOURCE_MANIFEST " +
                     std::to_string(kManifestFormatVersion) + "\nrevision " +
                     std::to_string(revision) + "\nrecords " +
                     std::to_string(records.size()) + "\n";
  for (const StoredRecord &record : records) {
    body += "record " + kind_name(record.source.kind) + " " +
            provenance_name(record.source.provenance) + " " +
            std::to_string(record.source.session_epoch) + " " +
            std::to_string(record.source.event_sequence) + " ";
    if (!append_double(&body, record.source.monotonic_time_s)) {
      return {};
    }
    body += " " +
            hex_encode(record.source.capture_id.data(),
                       record.source.capture_id.size()) +
            " " +
            hex_encode(record.source.anchor_id.data(),
                       record.source.anchor_id.size()) +
            " " + std::to_string(record.source.source_revision) + " " +
            std::to_string(record.source.payload_version) + " " +
            std::to_string(record.payload_length) + " " +
            hex_encode(record.sha256.data(), record.sha256.size()) + " " +
            record.relative_path + "\n";
  }
  const std::vector<uint8_t> body_bytes(body.begin(), body.end());
  return body + "manifest_sha256 " +
         hex_encode(sha256(body_bytes).data(), 32U) + "\n";
}

struct ParsedManifest {
  uint64_t revision{0};
  std::vector<StoredRecord> records;
};

std::optional<ParsedManifest> parse_manifest(const std::vector<uint8_t> &bytes,
                                             const RetentionLimits &limits) {
  const std::string text(bytes.begin(), bytes.end());
  const size_t marker = text.rfind("manifest_sha256 ");
  if (marker == std::string::npos || marker == 0U ||
      marker + 81U != text.size() || text.back() != '\n') {
    return std::nullopt;
  }
  const std::string body = text.substr(0, marker);
  const std::string expected_hex = text.substr(marker + 16U, 64U);
  const auto expected = hex_decode(expected_hex);
  if (!expected || expected->size() != 32U ||
      !std::equal(
          expected->begin(), expected->end(),
          sha256(std::vector<uint8_t>(body.begin(), body.end())).begin())) {
    return std::nullopt;
  }

  std::istringstream stream(body);
  std::string line;
  if (!std::getline(stream, line) || line != "HSS_SOURCE_MANIFEST 1")
    return std::nullopt;
  if (!std::getline(stream, line) || line.rfind("revision ", 0U) != 0U)
    return std::nullopt;
  ParsedManifest manifest;
  if (!parse_unsigned<uint64_t>(std::string_view(line).substr(9U),
                                &manifest.revision))
    return std::nullopt;
  if (!std::getline(stream, line) || line.rfind("records ", 0U) != 0U)
    return std::nullopt;
  uint64_t declared_count = 0;
  if (!parse_unsigned<uint64_t>(std::string_view(line).substr(8U),
                                &declared_count) ||
      declared_count > limits.max_epoch_records + limits.max_depth_records +
                           limits.max_mesh_records + limits.max_command_records)
    return std::nullopt;

  for (uint64_t index = 0; index < declared_count; ++index) {
    if (!std::getline(stream, line))
      return std::nullopt;
    std::istringstream fields(line);
    std::array<std::string, 13> values{};
    for (std::string &value : values) {
      if (!(fields >> value))
        return std::nullopt;
    }
    std::string unexpected;
    if (fields >> unexpected || values[0] != "record")
      return std::nullopt;
    const auto kind = parse_kind(values[1]);
    const auto provenance = parse_provenance(values[2]);
    const auto capture = hex_decode(values[6]);
    const auto anchor = hex_decode(values[7]);
    const auto digest = hex_decode(values[11]);
    StoredRecord record;
    if (!kind || !provenance || !capture ||
        capture->size() != record.source.capture_id.size() || !anchor ||
        anchor->size() != record.source.anchor_id.size() || !digest ||
        digest->size() != record.sha256.size() ||
        !parse_unsigned<uint32_t>(values[3], &record.source.session_epoch) ||
        !parse_unsigned<uint64_t>(values[4], &record.source.event_sequence) ||
        !parse_double(values[5], &record.source.monotonic_time_s) ||
        !parse_unsigned<uint64_t>(values[8], &record.source.source_revision) ||
        !parse_unsigned<uint32_t>(values[9], &record.source.payload_version) ||
        !parse_unsigned<uint64_t>(values[10], &record.payload_length)) {
      return std::nullopt;
    }
    record.source.kind = *kind;
    record.source.provenance = *provenance;
    std::copy(capture->begin(), capture->end(),
              record.source.capture_id.begin());
    std::copy(anchor->begin(), anchor->end(), record.source.anchor_id.begin());
    std::copy(digest->begin(), digest->end(), record.sha256.begin());
    record.relative_path = values[12];
    if (!is_manifest_record_valid(record, limits))
      return std::nullopt;
    manifest.records.push_back(std::move(record));
  }
  if (std::getline(stream, line))
    return std::nullopt;
  if (!std::is_sorted(manifest.records.begin(), manifest.records.end(),
                      less_replay_order))
    return std::nullopt;
  for (size_t index = 1; index < manifest.records.size(); ++index) {
    if (manifest.records[index - 1U].source.event_sequence ==
        manifest.records[index].source.event_sequence) {
      return std::nullopt;
    }
  }
  return manifest;
}

bool manifest_assets_are_valid(const ParsedManifest &manifest,
                               const FileSystem &file_system,
                               const RetentionLimits &limits) {
  uint64_t total_bytes = 0;
  uint64_t depth_count = 0;
  uint64_t mesh_count = 0;
  uint64_t epoch_count = 0;
  uint64_t command_count = 0;
  for (const StoredRecord &record : manifest.records) {
    if (record.payload_length > limits.max_chunk_bytes ||
        total_bytes > limits.max_retained_bytes - record.payload_length) {
      return false;
    }
    total_bytes += record.payload_length;
    depth_count += record.source.kind == SourceKind::kDepth ? 1U : 0U;
    mesh_count += record.source.kind == SourceKind::kMesh ? 1U : 0U;
    epoch_count += record.source.kind == SourceKind::kEpoch ? 1U : 0U;
    command_count += (record.source.kind == SourceKind::kCommand ||
                      record.source.kind == SourceKind::kPlanReference)
                         ? 1U
                         : 0U;
    std::vector<uint8_t> payload;
    if (!file_system.read_file_bounded(record.relative_path,
                                       record.payload_length, &payload) ||
        payload.size() != record.payload_length ||
        sha256(payload) != record.sha256) {
      return false;
    }
  }
  return epoch_count <= limits.max_epoch_records &&
         depth_count <= limits.max_depth_records &&
         mesh_count <= limits.max_mesh_records &&
         command_count <= limits.max_command_records;
}

std::optional<ParsedManifest>
validated_manifest(const FileSystem &file_system, const std::string &path,
                   const RetentionLimits &limits) {
  std::vector<uint8_t> bytes;
  if (!file_system.read_file_bounded(path, kMaximumManifestBytes, &bytes))
    return std::nullopt;
  const auto manifest = parse_manifest(bytes, limits);
  if (!manifest || !manifest_assets_are_valid(*manifest, file_system, limits))
    return std::nullopt;
  return manifest;
}

std::vector<uint8_t> as_bytes(const std::string &text) {
  return {text.begin(), text.end()};
}

std::vector<uint8_t>
serialize_project_metadata(const ProjectMetadata &metadata) {
  const std::string body =
      "{\"format\":\"hss.project\",\"version\":1,\"project_id\":\"" +
      hex_encode(metadata.project_id.data(), metadata.project_id.size()) +
      "\",\"source_is_synthetic\":" +
      (metadata.source_is_synthetic ? "true" : "false") + "}";
  const std::vector<uint8_t> body_bytes = as_bytes(body);
  const std::string complete =
      body.substr(0U, body.size() - 1U) + ",\"metadata_sha256\":\"" +
      hex_encode(sha256(body_bytes).data(), 32U) + "\"}\n";
  return as_bytes(complete);
}

std::optional<ProjectMetadata>
parse_project_metadata(const std::vector<uint8_t> &bytes) {
  const std::string text(bytes.begin(), bytes.end());
  constexpr std::string_view prefix =
      "{\"format\":\"hss.project\",\"version\":1,\"project_id\":\"";
  constexpr std::string_view boolean_prefix = "\",\"source_is_synthetic\":";
  constexpr std::string_view checksum_prefix = ",\"metadata_sha256\":\"";
  if (text.size() < prefix.size() + 32U + boolean_prefix.size() + 4U +
                        checksum_prefix.size() + 64U + 3U ||
      text.rfind(prefix, 0U) != 0U || text.back() != '\n') {
    return std::nullopt;
  }
  const size_t id_begin = prefix.size();
  const size_t boolean_begin = id_begin + 32U;
  if (text.compare(boolean_begin, boolean_prefix.size(), boolean_prefix) != 0)
    return std::nullopt;
  const size_t value_begin = boolean_begin + boolean_prefix.size();
  bool synthetic = false;
  size_t body_end = 0U;
  if (text.compare(value_begin, 5U, "false") == 0) {
    body_end = value_begin + 5U;
  } else if (text.compare(value_begin, 4U, "true") == 0) {
    synthetic = true;
    body_end = value_begin + 4U;
  } else {
    return std::nullopt;
  }
  if (text.compare(body_end, checksum_prefix.size(), checksum_prefix) != 0 ||
      body_end + checksum_prefix.size() + 64U + 3U != text.size() ||
      text.compare(text.size() - 3U, 3U, "\"}\n") != 0) {
    return std::nullopt;
  }
  const auto project_id =
      hex_decode(std::string_view(text).substr(id_begin, 32U));
  const auto checksum = hex_decode(
      std::string_view(text).substr(body_end + checksum_prefix.size(), 64U));
  if (!project_id || project_id->size() != 16U || !checksum ||
      checksum->size() != 32U) {
    return std::nullopt;
  }
  const std::string body = text.substr(0U, body_end) + "}";
  const Digest actual = sha256(as_bytes(body));
  if (!std::equal(checksum->begin(), checksum->end(), actual.begin()))
    return std::nullopt;
  ProjectMetadata metadata;
  std::copy(project_id->begin(), project_id->end(),
            metadata.project_id.begin());
  metadata.source_is_synthetic = synthetic;
  return metadata;
}

bool write_immutable_chunk(FileSystem &file_system, const StoredRecord &record,
                           const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> existing;
  if (file_system.exists(record.relative_path)) {
    if (file_system.read_file_bounded(record.relative_path,
                                      record.payload_length, &existing) &&
        existing == payload) {
      return true;
    }
    // A name includes the expected digest, so a different existing value is a
    // damaged/uncommitted artifact, never a second source revision. Replace it
    // through the same temporary+rename path before it is referenced again.
  }
  const std::string temporary =
      "temporary/" + record.relative_path.substr(7U) + ".pending";
  if (!file_system.write_file(temporary, payload) ||
      !file_system.replace_atomically(temporary, record.relative_path) ||
      !file_system.sync_directory("chunks")) {
    return false;
  }
  return file_system.read_file_bounded(record.relative_path,
                                       record.payload_length, &existing) &&
         existing == payload;
}

bool fits_retention(const std::vector<StoredRecord> &existing,
                    const std::vector<SourceRecord> &incoming,
                    const RetentionLimits &limits) {
  uint64_t total_bytes = 0;
  uint64_t depth_count = 0;
  uint64_t mesh_count = 0;
  uint64_t epoch_count = 0;
  uint64_t command_count = 0;
  const auto include = [&total_bytes, &depth_count, &mesh_count, &epoch_count,
                        &command_count,
                        &limits](SourceKind kind, uint64_t payload_length) {
    if (payload_length > limits.max_retained_bytes - total_bytes)
      return false;
    total_bytes += payload_length;
    switch (kind) {
    case SourceKind::kEpoch:
      if (epoch_count == limits.max_epoch_records)
        return false;
      ++epoch_count;
      break;
    case SourceKind::kDepth:
      if (depth_count == limits.max_depth_records)
        return false;
      ++depth_count;
      break;
    case SourceKind::kMesh:
      if (mesh_count == limits.max_mesh_records)
        return false;
      ++mesh_count;
      break;
    case SourceKind::kCommand:
    case SourceKind::kPlanReference:
      if (command_count == limits.max_command_records)
        return false;
      ++command_count;
      break;
    }
    return true;
  };
  for (const StoredRecord &record : existing) {
    if (!include(record.source.kind, record.payload_length))
      return false;
  }
  for (const SourceRecord &record : incoming) {
    if (!include(record.kind, static_cast<uint64_t>(record.payload.size())))
      return false;
  }
  return true;
}

void garbage_collect(FileSystem &file_system, const RetentionLimits &limits) {
  std::vector<std::string> reachable;
  if (const auto active =
          validated_manifest(file_system, kActiveManifest, limits)) {
    for (const StoredRecord &record : active->records)
      reachable.push_back(record.relative_path);
  }
  if (const auto previous =
          validated_manifest(file_system, kPreviousManifest, limits)) {
    for (const StoredRecord &record : previous->records)
      reachable.push_back(record.relative_path);
  }
  std::sort(reachable.begin(), reachable.end());
  reachable.erase(std::unique(reachable.begin(), reachable.end()),
                  reachable.end());
  std::vector<std::string> chunks;
  if (file_system.list_files("chunks", &chunks)) {
    for (const std::string &chunk : chunks) {
      if (!std::binary_search(reachable.begin(), reachable.end(), chunk)) {
        static_cast<void>(file_system.remove_file(chunk));
      }
    }
    static_cast<void>(file_system.sync_directory("chunks"));
  }
  // Temporary assets are not in any manifest and are never a recovery source.
  std::vector<std::string> temporary;
  if (file_system.list_files("temporary", &temporary)) {
    for (const std::string &path : temporary)
      static_cast<void>(file_system.remove_file(path));
    static_cast<void>(file_system.sync_directory("temporary"));
  }
}

} // namespace

NativeFileSystem::NativeFileSystem(std::string root_path)
    : root_path_(std::move(root_path)) {}

bool FileSystem::read_file_bounded(const std::string &relative_path,
                                   uint64_t maximum_bytes,
                                   std::vector<uint8_t> *bytes) const {
  if (bytes == nullptr || !read_file(relative_path, bytes))
    return false;
  if (bytes->size() > maximum_bytes) {
    bytes->clear();
    return false;
  }
  return true;
}

std::string
NativeFileSystem::absolute_path(const std::string &relative_path) const {
  return (std::filesystem::path(root_path_) /
          std::filesystem::path(relative_path))
      .string();
}

bool NativeFileSystem::create_directories(const std::string &relative_path) {
  std::error_code error;
  std::filesystem::create_directories(absolute_path(relative_path), error);
  return !error;
}

bool NativeFileSystem::write_file(const std::string &relative_path,
                                  const std::vector<uint8_t> &bytes) {
  const int descriptor = ::open(absolute_path(relative_path).c_str(),
                                O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (descriptor < 0)
    return false;
  size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t written =
        ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
    if (written <= 0) {
      ::close(descriptor);
      return false;
    }
    offset += static_cast<size_t>(written);
  }
  const bool synced = ::fsync(descriptor) == 0;
  const bool closed = ::close(descriptor) == 0;
  return synced && closed;
}

bool NativeFileSystem::read_file(const std::string &relative_path,
                                 std::vector<uint8_t> *bytes) const {
  return read_file_bounded(relative_path, kMaximumManifestBytes, bytes);
}

bool NativeFileSystem::read_file_bounded(const std::string &relative_path,
                                         uint64_t maximum_bytes,
                                         std::vector<uint8_t> *bytes) const {
  if (bytes == nullptr)
    return false;
  std::error_code error;
  const auto path = absolute_path(relative_path);
  if (!std::filesystem::is_regular_file(path, error) || error)
    return false;
  const uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > maximum_bytes ||
      size > static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
    return false;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return false;
  try {
    bytes->assign(static_cast<size_t>(size), 0U);
    if (size != 0U)
      stream.read(reinterpret_cast<char *>(bytes->data()),
                  static_cast<std::streamsize>(size));
    if (stream.good() || stream.eof())
      return true;
  } catch (const std::bad_alloc &) {
    // A failed allocation is equivalent to an unreadable candidate revision.
  }
  bytes->clear();
  return false;
}

bool NativeFileSystem::exists(const std::string &relative_path) const {
  std::error_code error;
  return std::filesystem::exists(absolute_path(relative_path), error) && !error;
}

bool NativeFileSystem::list_files(
    const std::string &relative_directory,
    std::vector<std::string> *relative_paths) const {
  if (relative_paths == nullptr)
    return false;
  std::error_code error;
  const std::filesystem::path directory(absolute_path(relative_directory));
  if (!std::filesystem::is_directory(directory, error) || error)
    return false;
  relative_paths->clear();
  for (const auto &entry :
       std::filesystem::directory_iterator(directory, error)) {
    if (error || !entry.is_regular_file(error) || error)
      return false;
    relative_paths->push_back(
        (std::filesystem::path(relative_directory) / entry.path().filename())
            .generic_string());
  }
  std::sort(relative_paths->begin(), relative_paths->end());
  return true;
}

bool NativeFileSystem::replace_atomically(const std::string &from_relative_path,
                                          const std::string &to_relative_path) {
  std::error_code error;
  std::filesystem::rename(absolute_path(from_relative_path),
                          absolute_path(to_relative_path), error);
  return !error;
}

bool NativeFileSystem::remove_file(const std::string &relative_path) {
  std::error_code error;
  std::filesystem::remove(absolute_path(relative_path), error);
  return !error;
}

bool NativeFileSystem::sync_directory(const std::string &relative_path) {
  const int descriptor =
      ::open(absolute_path(relative_path).c_str(), O_RDONLY | O_DIRECTORY);
  if (descriptor < 0)
    return false;
  const bool synced = ::fsync(descriptor) == 0;
  const bool closed = ::close(descriptor) == 0;
  return synced && closed;
}

DurableSourceStore::DurableSourceStore(FileSystem &file_system,
                                       RetentionLimits limits)
    : file_system_(file_system), limits_(limits) {}

StoreStatus DurableSourceStore::open_project(const ProjectMetadata &metadata,
                                             ProjectOpenMode mode) {
  if (!valid_limits(limits_) || is_zero(metadata.project_id) ||
      (mode != ProjectOpenMode::kCreateNew &&
       mode != ProjectOpenMode::kOpenExisting &&
       mode != ProjectOpenMode::kOpenOrCreate)) {
    return StoreStatus::kInvalidArgument;
  }
  if (!file_system_.create_directories(".") ||
      !file_system_.create_directories("temporary")) {
    return StoreStatus::kIoError;
  }
  if (file_system_.exists(kProjectMetadata)) {
    if (mode == ProjectOpenMode::kCreateNew)
      return StoreStatus::kAlreadyExists;
    std::vector<uint8_t> bytes;
    if (!file_system_.read_file_bounded(kProjectMetadata, 4096U, &bytes))
      return StoreStatus::kCorrupt;
    const auto stored = parse_project_metadata(bytes);
    if (!stored)
      return StoreStatus::kCorrupt;
    if (stored->project_id != metadata.project_id ||
        stored->source_is_synthetic != metadata.source_is_synthetic) {
      return StoreStatus::kProjectMismatch;
    }
    return StoreStatus::kOk;
  }
  if (mode == ProjectOpenMode::kOpenExisting)
    return StoreStatus::kNotFound;
  const std::vector<uint8_t> bytes = serialize_project_metadata(metadata);
  if (!file_system_.write_file(kProjectMetadataTemporary, bytes) ||
      !file_system_.replace_atomically(kProjectMetadataTemporary,
                                       kProjectMetadata) ||
      !file_system_.sync_directory(".")) {
    return StoreStatus::kIoError;
  }
  std::vector<uint8_t> verified;
  if (!file_system_.read_file_bounded(kProjectMetadata, 4096U, &verified) ||
      verified != bytes) {
    return StoreStatus::kCorrupt;
  }
  return StoreStatus::kOk;
}

RecoveryResult DurableSourceStore::recover() const {
  if (!valid_limits(limits_))
    return {.status = StoreStatus::kInvalidArgument,
            .revision = 0U,
            .used_previous_manifest = false,
            .records = {}};
  if (const auto active =
          validated_manifest(file_system_, kActiveManifest, limits_)) {
    return {.status = StoreStatus::kOk,
            .revision = active->revision,
            .records = active->records};
  }
  if (const auto previous =
          validated_manifest(file_system_, kPreviousManifest, limits_)) {
    return {.status = StoreStatus::kOk,
            .revision = previous->revision,
            .used_previous_manifest = true,
            .records = previous->records};
  }
  if (!file_system_.exists(kActiveManifest) &&
      !file_system_.exists(kPreviousManifest)) {
    return {.status = StoreStatus::kOk,
            .revision = 0U,
            .used_previous_manifest = false,
            .records = {}};
  }
  return {.status = StoreStatus::kCorrupt,
          .revision = 0U,
          .used_previous_manifest = false,
          .records = {}};
}

StoreStatus DurableSourceStore::checkpoint(
    const std::vector<SourceRecord> &selected_records) {
  if (!valid_limits(limits_))
    return StoreStatus::kInvalidArgument;
  const RecoveryResult before = recover();
  if (before.status != StoreStatus::kOk)
    return before.status;
  if (before.revision == std::numeric_limits<uint64_t>::max())
    return StoreStatus::kResourceLimit;
  for (size_t index = 0; index < selected_records.size(); ++index) {
    if (!valid_record(selected_records[index], limits_) ||
        (index != 0U && selected_records[index - 1U].event_sequence >=
                            selected_records[index].event_sequence) ||
        (!before.records.empty() &&
         selected_records[index].event_sequence <=
             before.records.back().source.event_sequence)) {
      return StoreStatus::kInvalidArgument;
    }
  }
  // Selected capture evidence is not silently evictable. Rebuild/refinement
  // relies on every source referenced by the active retained checkpoint; a
  // future pinning/eviction policy must make that authority explicit.
  if (!fits_retention(before.records, selected_records, limits_))
    return StoreStatus::kResourceLimit;
  if (!file_system_.create_directories("chunks") ||
      !file_system_.create_directories("temporary")) {
    return StoreStatus::kIoError;
  }

  std::vector<StoredRecord> all_records = before.records;
  for (const SourceRecord &source : selected_records) {
    StoredRecord stored;
    stored.source.kind = source.kind;
    stored.source.provenance = source.provenance;
    stored.source.capture_id = source.capture_id;
    stored.source.anchor_id = source.anchor_id;
    stored.source.session_epoch = source.session_epoch;
    stored.source.event_sequence = source.event_sequence;
    stored.source.monotonic_time_s = source.monotonic_time_s;
    stored.source.source_revision = source.source_revision;
    stored.source.payload_version = source.payload_version;
    stored.payload_length = source.payload.size();
    stored.sha256 = sha256(source.payload);
    stored.relative_path = chunk_path(stored);
    if (!write_immutable_chunk(file_system_, stored, source.payload))
      return StoreStatus::kIoError;
    all_records.push_back(std::move(stored));
  }
  std::sort(all_records.begin(), all_records.end(), less_replay_order);
  const uint64_t next_revision = before.revision + 1U;
  const std::string manifest = serialize_manifest(next_revision, all_records);
  if (manifest.empty())
    return StoreStatus::kInvalidArgument;
  const std::vector<uint8_t> manifest_bytes = as_bytes(manifest);
  if (!file_system_.write_file(kManifestTemporary, manifest_bytes))
    return StoreStatus::kIoError;
  const auto parsed = parse_manifest(manifest_bytes, limits_);
  if (!parsed || !manifest_assets_are_valid(*parsed, file_system_, limits_))
    return StoreStatus::kCorrupt;

  // Write the previous validated manifest as a separate durable replacement.
  // Active remains intact until the next manifest is fully flushed and
  // verified.
  if (file_system_.exists(kActiveManifest)) {
    std::vector<uint8_t> old_active;
    if (!before.used_previous_manifest &&
        validated_manifest(file_system_, kActiveManifest, limits_)
            .has_value()) {
      if (!file_system_.read_file_bounded(kActiveManifest,
                                          kMaximumManifestBytes, &old_active)) {
        return StoreStatus::kIoError;
      }
    } else {
      old_active =
          as_bytes(serialize_manifest(before.revision, before.records));
    }
    if (old_active.empty() ||
        !file_system_.write_file(kPreviousTemporary, old_active) ||
        !file_system_.replace_atomically(kPreviousTemporary,
                                         kPreviousManifest) ||
        !file_system_.sync_directory(".")) {
      return StoreStatus::kIoError;
    }
  }
  if (!file_system_.replace_atomically(kManifestTemporary, kActiveManifest) ||
      !file_system_.sync_directory(".")) {
    return StoreStatus::kIoError;
  }
  garbage_collect(file_system_, limits_);
  return StoreStatus::kOk;
}

StoreStatus
DurableSourceStore::read_payload(const StoredRecord &record,
                                 std::vector<uint8_t> *payload) const {
  if (payload == nullptr || !is_manifest_record_valid(record, limits_) ||
      !file_system_.read_file_bounded(record.relative_path,
                                      record.payload_length, payload) ||
      payload->size() != record.payload_length ||
      sha256(*payload) != record.sha256) {
    return StoreStatus::kCorrupt;
  }
  return StoreStatus::kOk;
}

} // namespace hss::storage
