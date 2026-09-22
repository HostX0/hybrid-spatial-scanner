#include "hss/project_import_validation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <new>
#include <set>
#include <string_view>

namespace hss::project_import {
namespace {

constexpr size_t kStreamChunkBytes = 64U * 1024U;

bool add_fits(uint64_t left, uint64_t right, uint64_t* result) {
  if (right > std::numeric_limits<uint64_t>::max() - left) return false;
  *result = left + right;
  return true;
}

bool is_drive_prefix(std::string_view path) {
  return path.size() >= 2U &&
         std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':';
}

struct PathCheck {
  Status status{Status::kOk};
  std::string normalized;
};

PathCheck inspect_path(const std::string& path, uint64_t max_path_bytes) {
  if (path.empty() || path.size() > max_path_bytes || path.find('\0') != std::string::npos ||
      path.front() == '/' || path.find('\\') != std::string::npos || is_drive_prefix(path)) {
    return {Status::kInvalidPath, {}};
  }

  bool noncanonical = false;
  std::vector<std::string_view> components;
  size_t position = 0;
  while (position < path.size()) {
    const size_t slash = path.find('/', position);
    const size_t end = slash == std::string::npos ? path.size() : slash;
    const std::string_view component(path.data() + position, end - position);
    if (component.empty()) return {Status::kInvalidPath, {}};
    if (component == ".") {
      noncanonical = true;
    } else if (component == "..") {
      noncanonical = true;
      if (components.empty()) return {Status::kInvalidPath, {}};
      components.pop_back();
    } else {
      components.push_back(component);
    }
    if (slash == std::string::npos) break;
    position = slash + 1U;
  }
  if (components.empty()) return {Status::kInvalidPath, {}};

  std::string normalized;
  for (const std::string_view component : components) {
    if (!normalized.empty()) normalized.push_back('/');
    normalized.append(component);
  }
  return {noncanonical ? Status::kInvalidPath : Status::kOk, std::move(normalized)};
}

uint64_t per_entry_limit(ContentKind kind, const Limits& limits) {
  switch (kind) {
    case ContentKind::kBinary:
      return limits.max_binary_entry_bytes;
    case ContentKind::kJson:
      return limits.max_json_entry_bytes;
    case ContentKind::kOther:
      return limits.max_other_entry_bytes;
  }
  return 0;
}

bool supported_content_kind(ContentKind kind) {
  return kind == ContentKind::kBinary || kind == ContentKind::kJson || kind == ContentKind::kOther;
}

class Sha256Hasher {
 public:
  Sha256Hasher() = default;

  void update(const uint8_t* data, size_t size) {
    total_bytes_ += static_cast<uint64_t>(size);
    while (size != 0U) {
      const size_t available = block_.size() - block_size_;
      const size_t copied = std::min(available, size);
      std::copy_n(data, copied, block_.data() + block_size_);
      block_size_ += copied;
      data += copied;
      size -= copied;
      if (block_size_ == block_.size()) {
        transform(block_.data());
        block_size_ = 0;
      }
    }
  }

  Sha256 finish() {
    const uint64_t bit_count = total_bytes_ * 8U;
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56U) {
      std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.end(), 0U);
      transform(block_.data());
      block_size_ = 0;
    }
    std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.begin() + 56, 0U);
    for (size_t index = 0; index < 8U; ++index) {
      block_[63U - index] = static_cast<uint8_t>(bit_count >> (8U * index));
    }
    transform(block_.data());
    Sha256 output{};
    for (size_t index = 0; index < state_.size(); ++index) {
      const uint32_t word = state_[index];
      for (size_t byte = 0; byte < 4U; ++byte) {
        output[index * 4U + byte] = static_cast<uint8_t>(word >> (24U - 8U * byte));
      }
    }
    return output;
  }

 private:
  static uint32_t rotate_right(uint32_t value, uint32_t shift) {
    return (value >> shift) | (value << (32U - shift));
  }

  void transform(const uint8_t* block) {
    static constexpr std::array<uint32_t, 64> kConstants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
        0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
        0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
        0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
        0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
        0xc67178f2U};
    std::array<uint32_t, 64> words{};
    for (size_t index = 0; index < 16U; ++index) {
      words[index] = (static_cast<uint32_t>(block[index * 4U]) << 24U) |
                     (static_cast<uint32_t>(block[index * 4U + 1U]) << 16U) |
                     (static_cast<uint32_t>(block[index * 4U + 2U]) << 8U) |
                     static_cast<uint32_t>(block[index * 4U + 3U]);
    }
    for (size_t index = 16U; index < words.size(); ++index) {
      const uint32_t small0 = rotate_right(words[index - 15U], 7U) ^ rotate_right(words[index - 15U], 18U) ^
                              (words[index - 15U] >> 3U);
      const uint32_t small1 = rotate_right(words[index - 2U], 17U) ^ rotate_right(words[index - 2U], 19U) ^
                              (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + small0 + words[index - 7U] + small1;
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
      const uint32_t big1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
      const uint32_t choose = (e & f) ^ (~e & g);
      const uint32_t temp1 = h + big1 + choose + kConstants[index] + words[index];
      const uint32_t big0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = big0 + majority;
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

  std::array<uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<uint8_t, 64> block_{};
  size_t block_size_{0};
  uint64_t total_bytes_{0};
};

Status validate_stream(const ArchiveEntry& entry) {
  try {
    std::array<uint8_t, kStreamChunkBytes> buffer{};
    uint64_t observed = 0;
    Sha256Hasher hasher;
    while (observed < entry.declared_uncompressed_bytes) {
      const uint64_t remaining = entry.declared_uncompressed_bytes - observed;
      const size_t requested = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
      const ReadResult result = entry.reader->read(buffer.data(), requested);
      if (result.state == ReadState::kFailure || result.bytes > requested ||
          (result.state != ReadState::kData && result.bytes != 0U) ||
          (result.state == ReadState::kData && result.bytes == 0U)) {
        return Status::kReaderFailure;
      }
      if (result.state == ReadState::kEnd) return Status::kLengthMismatch;
      hasher.update(buffer.data(), result.bytes);
      observed += static_cast<uint64_t>(result.bytes);
    }
    const ReadResult trailing = entry.reader->read(buffer.data(), 1U);
    if (trailing.state == ReadState::kFailure || trailing.bytes > 1U ||
        (trailing.state != ReadState::kData && trailing.bytes != 0U) ||
        (trailing.state == ReadState::kData && trailing.bytes == 0U)) {
      return Status::kReaderFailure;
    }
    if (trailing.state != ReadState::kEnd) return Status::kLengthMismatch;
    return hasher.finish() == entry.sha256 ? Status::kOk : Status::kChecksumMismatch;
  } catch (...) {
    return Status::kReaderFailure;
  }
}

ValidationResult failed(Status status) { return {status, {}}; }

}  // namespace

ValidationResult validate_archive(const std::vector<ArchiveEntry>& entries, const Limits& limits) {
  try {
    if (limits.max_file_count == 0U || limits.max_expanded_bytes == 0U || limits.max_path_bytes == 0U) {
      return failed(Status::kInvalidArgument);
    }
    uint64_t file_count = 0;
    uint64_t expanded_bytes = 0;
    std::set<std::string> normalized_paths;
    for (const ArchiveEntry& entry : entries) {
      const PathCheck path = inspect_path(entry.path, limits.max_path_bytes);
      if (!path.normalized.empty() && !normalized_paths.insert(path.normalized).second) {
        return failed(Status::kDuplicatePath);
      }
      if (path.status != Status::kOk) return failed(path.status);
      if (entry.kind == EntryKind::kSymbolicLink || entry.kind == EntryKind::kHardLink) {
        return failed(Status::kUnsupportedEntry);
      }
      if (entry.kind == EntryKind::kDirectory) {
        if (entry.declared_uncompressed_bytes != 0U || entry.reader != nullptr || entry.has_sha256) {
          return failed(Status::kInvalidArgument);
        }
        continue;
      }
      if (entry.kind != EntryKind::kRegularFile || !supported_content_kind(entry.content_kind) ||
          entry.reader == nullptr || !entry.has_sha256 ||
          entry.declared_uncompressed_bytes > per_entry_limit(entry.content_kind, limits)) {
        return failed(entry.declared_uncompressed_bytes > per_entry_limit(entry.content_kind, limits)
                          ? Status::kResourceLimit
                          : Status::kInvalidArgument);
      }
      if (file_count >= limits.max_file_count) return failed(Status::kResourceLimit);
      uint64_t total = 0;
      if (!add_fits(expanded_bytes, entry.declared_uncompressed_bytes, &total) ||
          total > limits.max_expanded_bytes) {
        return failed(Status::kResourceLimit);
      }
      ++file_count;
      expanded_bytes = total;
    }

    std::vector<std::string> accepted_paths;
    accepted_paths.reserve(static_cast<size_t>(file_count));
    for (const ArchiveEntry& entry : entries) {
      if (entry.kind != EntryKind::kRegularFile) continue;
      const Status stream_status = validate_stream(entry);
      if (stream_status != Status::kOk) return failed(stream_status);
      accepted_paths.push_back(entry.path);
    }
    return {Status::kOk, std::move(accepted_paths)};
  } catch (const std::bad_alloc&) {
    return failed(Status::kResourceLimit);
  } catch (...) {
    return failed(Status::kInternalError);
  }
}

}  // namespace hss::project_import
