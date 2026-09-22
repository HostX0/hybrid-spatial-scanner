#include <algorithm>
#include "hss/durable_source_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

using hss::storage::DurableSourceStore;
using hss::storage::FileSystem;
using hss::storage::NativeFileSystem;
using hss::storage::RecoveryResult;
using hss::storage::RetentionLimits;
using hss::storage::SourceKind;
using hss::storage::SourceProvenance;
using hss::storage::SourceRecord;
using hss::storage::StoreStatus;

void check(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    char template_path[] = "/tmp/hss-b007-store-XXXXXX";
    char* created = ::mkdtemp(template_path);
    check(created != nullptr, "create temporary source store directory");
    path_ = created;
  }
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

SourceRecord record(SourceKind kind, uint64_t sequence, std::vector<uint8_t> payload) {
  SourceRecord result;
  result.kind = kind;
  result.capture_id[0] = 0x42U;
  result.capture_id[15] = static_cast<uint8_t>(sequence);
  if (kind == SourceKind::kMesh) {
    result.anchor_id[0] = 0x84U;
    result.anchor_id[15] = static_cast<uint8_t>(sequence);
    result.source_revision = sequence;
  }
  result.session_epoch = 3U;
  result.event_sequence = sequence;
  result.monotonic_time_s = static_cast<double>(sequence) / 10.0;
  result.payload = std::move(payload);
  return result;
}

class FailOnceFileSystem final : public FileSystem {
 public:
  FailOnceFileSystem(FileSystem& inner, std::string failing_path)
      : inner_(inner), failing_path_(std::move(failing_path)) {}

  bool create_directories(const std::string& path) override { return inner_.create_directories(path); }
  bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) override {
    if (!failed_ && path == failing_path_) {
      failed_ = true;
      // Deliberately leave a partial temporary write: recovery must not accept it.
      const std::vector<uint8_t> partial(bytes.begin(), bytes.begin() + (bytes.size() / 2U));
      static_cast<void>(inner_.write_file(path, partial));
      return false;
    }
    return inner_.write_file(path, bytes);
  }
  bool read_file(const std::string& path, std::vector<uint8_t>* bytes) const override {
    return inner_.read_file(path, bytes);
  }
  bool read_file_bounded(const std::string& path, uint64_t maximum_bytes,
                         std::vector<uint8_t>* bytes) const override {
    return inner_.read_file_bounded(path, maximum_bytes, bytes);
  }
  bool exists(const std::string& path) const override { return inner_.exists(path); }
  bool list_files(const std::string& directory, std::vector<std::string>* paths) const override {
    return inner_.list_files(directory, paths);
  }
  bool replace_atomically(const std::string& from, const std::string& to) override {
    return inner_.replace_atomically(from, to);
  }
  bool remove_file(const std::string& path) override { return inner_.remove_file(path); }
  bool sync_directory(const std::string& path) override { return inner_.sync_directory(path); }

 private:
  FileSystem& inner_;
  std::string failing_path_;
  bool failed_{false};
};

void write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  check(static_cast<bool>(output), "open deliberate corruption target");
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  output.close();
  check(static_cast<bool>(output), "write deliberate corruption target");
}

void test_owned_source_and_deterministic_replay() {
  TemporaryDirectory directory;
  NativeFileSystem filesystem(directory.path());
  DurableSourceStore store(filesystem, {});
  SourceRecord depth = record(SourceKind::kDepth, 10U, {1U, 2U, 3U});
  SourceRecord mesh = record(SourceKind::kMesh, 11U, {4U, 5U});
  check(store.checkpoint({depth, mesh}) == StoreStatus::kOk, "checkpoint selected depth and mesh");
  depth.payload[0] = 99U;
  mesh.payload[0] = 88U;
  RecoveryResult recovered = store.recover();
  check(recovered.status == StoreStatus::kOk && recovered.revision == 1U &&
            recovered.records.size() == 2U,
        "recover committed manifest");
  check(recovered.records[0].source.event_sequence == 10U &&
            recovered.records[1].source.event_sequence == 11U,
        "manifest replay ordering uses retained event sequence");
  check(recovered.records[0].source.session_epoch == 3U &&
            recovered.records[1].source.source_revision == 11U,
        "manifest retains epoch and mesh revision metadata");
  std::vector<uint8_t> restored;
  check(store.read_payload(recovered.records[0], &restored) == StoreStatus::kOk &&
            restored == std::vector<uint8_t>({1U, 2U, 3U}),
        "chunk owns copied source bytes rather than caller storage");
  check(store.checkpoint({record(SourceKind::kMesh, 12U, {})}) == StoreStatus::kOk,
        "zero-payload mesh removal with stable anchor identity is retained");
  recovered = store.recover();
  check(recovered.status == StoreStatus::kOk && recovered.records.size() == 3U &&
            recovered.records.back().source.event_sequence == 12U &&
            recovered.records.back().source.payload.empty() &&
            recovered.records.back().source.anchor_id[0] == 0x84U,
        "valid mesh removal round trips its nonzero anchor identity");
  restored = {1U};
  check(store.read_payload(recovered.records.back(), &restored) == StoreStatus::kOk && restored.empty(),
        "valid mesh removal reads an empty owned payload");
  SourceRecord anonymous_removal = record(SourceKind::kMesh, 13U, {});
  anonymous_removal.anchor_id.fill(0U);
  check(store.checkpoint({anonymous_removal}) == StoreStatus::kInvalidArgument,
        "write rejects zero-identity mesh removal");
  auto invalid_stored_removal = recovered.records.back();
  invalid_stored_removal.source.anchor_id.fill(0U);
  check(store.read_payload(invalid_stored_removal, &restored) == StoreStatus::kCorrupt,
        "stored/recovery validation rejects zero-identity mesh removal");
}

void test_partial_manifest_never_publishes() {
  TemporaryDirectory directory;
  NativeFileSystem native_filesystem(directory.path());
  DurableSourceStore healthy_store(native_filesystem, {});
  check(healthy_store.checkpoint({record(SourceKind::kDepth, 1U, {7U})}) == StoreStatus::kOk,
        "first checkpoint before injected interruption");
  FailOnceFileSystem interrupted_filesystem(native_filesystem, "temporary/manifest.pending");
  DurableSourceStore interrupted_store(interrupted_filesystem, {});
  check(interrupted_store.checkpoint({record(SourceKind::kMesh, 2U, {8U})}) == StoreStatus::kIoError,
        "partial manifest write reports IO failure");
  const RecoveryResult recovered = healthy_store.recover();
  check(recovered.status == StoreStatus::kOk && recovered.revision == 1U &&
            recovered.records.size() == 1U && recovered.records[0].source.event_sequence == 1U,
        "interrupted temporary manifest cannot become active revision");
}

void test_corruption_falls_back_to_previous_manifest() {
  TemporaryDirectory directory;
  NativeFileSystem filesystem(directory.path());
  DurableSourceStore store(filesystem, {});
  check(store.checkpoint({record(SourceKind::kDepth, 1U, {1U, 1U})}) == StoreStatus::kOk,
        "checkpoint baseline for chunk fallback");
  check(store.checkpoint({record(SourceKind::kMesh, 2U, {2U, 2U})}) == StoreStatus::kOk,
        "checkpoint revision with prior manifest");
  RecoveryResult active = store.recover();
  check(active.revision == 2U && active.records.size() == 2U, "active revision before corruption");
  std::filesystem::resize_file(std::filesystem::path(directory.path()) / "active.manifest",
                               16U * 1024U * 1024U + 1U);
  RecoveryResult fallback = store.recover();
  check(fallback.status == StoreStatus::kOk && fallback.used_previous_manifest && fallback.revision == 1U,
        "oversized active manifest falls back before allocation");
  check(store.checkpoint({record(SourceKind::kMesh, 2U, {2U, 2U})}) == StoreStatus::kOk,
        "checkpoint repairs oversized manifest from previous revision");
  active = store.recover();
  std::filesystem::resize_file(std::filesystem::path(directory.path()) / active.records.back().relative_path,
                               300U * 1024U * 1024U);
  fallback = store.recover();
  check(fallback.status == StoreStatus::kOk && fallback.used_previous_manifest && fallback.revision == 1U,
        "oversized chunk is rejected against declared length before allocation");
  check(store.checkpoint({record(SourceKind::kMesh, 2U, {2U, 2U})}) == StoreStatus::kOk,
        "checkpoint repairs oversized chunk from previous revision");
  active = store.recover();
  write_bytes((std::filesystem::path(directory.path()) / active.records.back().relative_path).string(), {0U, 1U});
  fallback = store.recover();
  check(fallback.status == StoreStatus::kOk && fallback.used_previous_manifest && fallback.revision == 1U &&
            fallback.records.size() == 1U,
        "bad chunk checksum falls back to prior validated revision");

  check(store.checkpoint({record(SourceKind::kMesh, 2U, {2U, 2U})}) == StoreStatus::kOk,
        "checkpoint repairs from validated fallback without exposing corrupt active manifest");
  active = store.recover();
  write_bytes((std::filesystem::path(directory.path()) / "active.manifest").string(), {0x00U, 0x01U});
  fallback = store.recover();
  check(fallback.status == StoreStatus::kOk && fallback.used_previous_manifest,
        "corrupt active manifest falls back to previous manifest");
}

void test_manifest_absence_and_non_evicting_retention() {
  TemporaryDirectory absent_directory;
  NativeFileSystem absent_filesystem(absent_directory.path());
  check(absent_filesystem.create_directories("chunks"), "create orphan chunk directory");
  check(absent_filesystem.write_file("chunks/orphan.bin", {9U, 9U}), "write orphaned incomplete chunk");
  DurableSourceStore absent_store(absent_filesystem, {});
  const RecoveryResult absent = absent_store.recover();
  check(absent.status == StoreStatus::kOk && absent.revision == 0U && absent.records.empty(),
        "manifest absence never accepts orphan chunk");

  TemporaryDirectory retained_directory;
  NativeFileSystem retained_filesystem(retained_directory.path());
  RetentionLimits limits;
  limits.max_depth_records = 2U;
  limits.max_mesh_records = 1U;
  limits.max_retained_bytes = 7U;
  limits.max_chunk_bytes = 4U;
  DurableSourceStore retained_store(retained_filesystem, limits);
  check(retained_store.checkpoint({record(SourceKind::kDepth, 1U, {1U, 1U})}) == StoreStatus::kOk,
        "first bounded source record");
  check(retained_store.checkpoint({record(SourceKind::kDepth, 2U, {2U, 2U})}) == StoreStatus::kOk,
        "second bounded source record");
  check(retained_store.checkpoint({record(SourceKind::kMesh, 3U, {3U, 3U})}) == StoreStatus::kOk,
        "retained mesh source record");
  check(retained_store.checkpoint({record(SourceKind::kDepth, 4U, {4U, 4U})}) == StoreStatus::kResourceLimit,
        "retention limit rejects instead of evicting selected source evidence");
  const RecoveryResult retained = retained_store.recover();
  check(retained.status == StoreStatus::kOk && retained.records.size() == 3U &&
            retained.records[0].source.event_sequence == 1U &&
            retained.records[1].source.event_sequence == 2U &&
            retained.records[2].source.event_sequence == 3U,
        "rejected checkpoint leaves selected replay evidence unchanged");
  std::vector<std::string> on_disk_chunks;
  check(retained_filesystem.list_files("chunks", &on_disk_chunks) && on_disk_chunks.size() == 3U,
        "limit rejection creates no orphaned replacement chunk");
  SourceRecord unknown = record(SourceKind::kDepth, 4U, {5U});
  unknown.provenance = static_cast<SourceProvenance>(99U);
  check(retained_store.checkpoint({unknown}) == StoreStatus::kInvalidArgument,
        "storage refuses unknown/generated provenance as captured source");
}

}  // namespace

int main() {
  test_owned_source_and_deterministic_replay();
  test_partial_manifest_never_publishes();
  test_corruption_falls_back_to_previous_manifest();
  test_manifest_absence_and_non_evicting_retention();
  std::cout << "durable source manifest-last storage checks passed\n";
  return EXIT_SUCCESS;
}
