#ifndef HSS_DURABLE_SOURCE_STORE_HPP
#define HSS_DURABLE_SOURCE_STORE_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hss::storage {

// This is deliberately source-only storage.  It accepts selected, already-owned
// capture bytes from platform adapters; it has no scene, inferred geometry, or
// rendering payload type that could accidentally make generated content look
// captured.
enum class SourceKind : uint8_t { kDepth = 1, kMesh = 2 };
enum class SourceProvenance : uint8_t { kCaptured = 1 };
enum class StoreStatus : uint8_t {
  kOk,
  kInvalidArgument,
  kResourceLimit,
  kIoError,
  kCorrupt,
};

struct SourceRecord {
  SourceKind kind{SourceKind::kDepth};
  SourceProvenance provenance{SourceProvenance::kCaptured};
  std::array<uint8_t, 16> capture_id{};
  // Zero for depth. Every mesh event, including a zero-payload removal, carries
  // its stable B-006 anchor UUID.
  std::array<uint8_t, 16> anchor_id{};
  uint32_t session_epoch{0};
  uint64_t event_sequence{0};
  double monotonic_time_s{0.0};
  uint64_t source_revision{0};
  uint32_t payload_version{1};
  // This value owns the copied source bytes. It never retains an ARKit or
  // Metal buffer/pointer. A zero-length mesh record can represent a removal.
  std::vector<uint8_t> payload;
};

struct RetentionLimits {
  uint64_t max_depth_records{128};
  uint64_t max_mesh_records{256};
  uint64_t max_retained_bytes{256U * 1024U * 1024U};
  uint64_t max_chunk_bytes{256U * 1024U * 1024U};
};

struct StoredRecord {
  SourceRecord source;
  uint64_t payload_length{0};
  std::array<uint8_t, 32> sha256{};
  std::string relative_path;
};

struct RecoveryResult {
  StoreStatus status{StoreStatus::kOk};
  uint64_t revision{0};
  bool used_previous_manifest{false};
  std::vector<StoredRecord> records;
};

// The small filesystem boundary keeps transaction logic host-testable. Its
// native implementation uses durable file writes and atomic replacement; tests
// can inject a failure at any write/replacement point without a Swift adapter.
class FileSystem {
 public:
  virtual ~FileSystem() = default;
  virtual bool create_directories(const std::string& relative_path) = 0;
  // Returns only after the file's contents are flushed and the file is closed.
  virtual bool write_file(const std::string& relative_path, const std::vector<uint8_t>& bytes) = 0;
  virtual bool read_file(const std::string& relative_path, std::vector<uint8_t>* bytes) const = 0;
  // Native implementations must reject a file larger than maximum_bytes before
  // allocating. The compatibility default is for existing test adapters only;
  // production adapters should override it with a size-first implementation.
  virtual bool read_file_bounded(const std::string& relative_path, uint64_t maximum_bytes,
                                 std::vector<uint8_t>* bytes) const;
  virtual bool exists(const std::string& relative_path) const = 0;
  virtual bool list_files(const std::string& relative_directory,
                          std::vector<std::string>* relative_paths) const = 0;
  virtual bool replace_atomically(const std::string& from_relative_path,
                                  const std::string& to_relative_path) = 0;
  virtual bool remove_file(const std::string& relative_path) = 0;
  virtual bool sync_directory(const std::string& relative_path) = 0;
};

// Native app/host implementation rooted at one trusted application directory.
// All store-owned paths are fixed relative paths, never capture-provided paths.
class NativeFileSystem final : public FileSystem {
 public:
  explicit NativeFileSystem(std::string root_path);
  bool create_directories(const std::string& relative_path) override;
  bool write_file(const std::string& relative_path, const std::vector<uint8_t>& bytes) override;
  bool read_file(const std::string& relative_path, std::vector<uint8_t>* bytes) const override;
  bool read_file_bounded(const std::string& relative_path, uint64_t maximum_bytes,
                         std::vector<uint8_t>* bytes) const override;
  bool exists(const std::string& relative_path) const override;
  bool list_files(const std::string& relative_directory,
                  std::vector<std::string>* relative_paths) const override;
  bool replace_atomically(const std::string& from_relative_path,
                          const std::string& to_relative_path) override;
  bool remove_file(const std::string& relative_path) override;
  bool sync_directory(const std::string& relative_path) override;

 private:
  std::string root_path_;
  std::string absolute_path(const std::string& relative_path) const;
};

class DurableSourceStore {
 public:
  DurableSourceStore(FileSystem& file_system, RetentionLimits limits);

  // Writes immutable chunks, verifies them, then writes and atomically
  // replaces the manifest. A failed checkpoint never becomes an active
  // revision; recovery serves the current or prior validated manifest only.
  StoreStatus checkpoint(std::vector<SourceRecord> selected_records);
  RecoveryResult recover() const;
  StoreStatus read_payload(const StoredRecord& record, std::vector<uint8_t>* payload) const;

 private:
  FileSystem& file_system_;
  RetentionLimits limits_;
};

}  // namespace hss::storage

#endif
