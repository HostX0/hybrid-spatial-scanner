#pragma once

#include "hss/coordinate_basis.hpp"
#include "hss/mesh_anchor_scene_entities.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace hss::mesh_anchor_scene_snapshot {

constexpr size_t kDefaultMaxEpochs = 32U;
constexpr size_t kDefaultMaxJsonBytes = 16U * 1024U * 1024U;
constexpr double kMaxAbsProjectCoordinateM = 10'000.0;

enum class Status : uint8_t {
  kOk,
  kEntitiesUnavailable,
  kInvalidArgument,
  kInvalidBasis,
  kInvalidEpoch,
  kResourceLimit,
};

struct EpochRecord {
  std::string capture_id;
  uint32_t session_epoch{};
  uint64_t transform_revision{};
  bool relationship_accepted{};
  std::optional<HSSMat4d> T_project_world;
};

struct Input {
  std::string project_id;
  uint64_t scene_revision{};
  bool source_is_synthetic{};
  coordinate::FloorFrame basis{};
  std::vector<EpochRecord> epochs;
};

struct Options {
  size_t max_epochs{kDefaultMaxEpochs};
  size_t max_json_bytes{kDefaultMaxJsonBytes};
};

struct Result {
  Status status{Status::kOk};
  std::string scene_json;
  size_t emitted_json_bytes{};
};

namespace detail {

inline bool is_lower_hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

inline bool canonical_uuid(std::string_view value) {
  if (value.size() != 36U)
    return false;
  for (size_t i = 0U; i < value.size(); ++i) {
    if (i == 8U || i == 13U || i == 18U || i == 23U) {
      if (value[i] != '-')
        return false;
    } else if (!is_lower_hex(value[i])) {
      return false;
    }
  }
  return true;
}

inline bool bounded(HSSVec3d p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::abs(p.x) <= kMaxAbsProjectCoordinateM &&
         std::abs(p.y) <= kMaxAbsProjectCoordinateM &&
         std::abs(p.z) <= kMaxAbsProjectCoordinateM;
}

inline std::string num(double value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(17) << value;
  return stream.str();
}

inline std::string vec(HSSVec3d value) {
  return "[" + num(value.x) + "," + num(value.y) + "," + num(value.z) + "]";
}

inline bool checked_add(size_t a, size_t b, size_t &out) {
  if (a > std::numeric_limits<size_t>::max() - b)
    return false;
  out = a + b;
  return true;
}

inline bool valid_epoch(const EpochRecord &epoch) {
  if (!canonical_uuid(epoch.capture_id) || epoch.transform_revision == 0U)
    return false;
  if (epoch.relationship_accepted) {
    return epoch.T_project_world.has_value() &&
           coordinate::is_rigid_transform(*epoch.T_project_world);
  }
  return !epoch.T_project_world.has_value();
}

inline std::string epoch_json(const EpochRecord &epoch) {
  std::string out = "{\"capture_id\":\"" + epoch.capture_id +
                    "\",\"session_epoch\":" +
                    std::to_string(epoch.session_epoch) +
                    ",\"transform_revision\":\"" +
                    std::to_string(epoch.transform_revision) +
                    "\",\"relationship_state\":\"" +
                    (epoch.relationship_accepted ? "accepted" : "unresolved") +
                    "\",\"T_project_world\":";
  if (!epoch.relationship_accepted) {
    out += "null}";
    return out;
  }
  out += '[';
  const auto &m = epoch.T_project_world->column_major;
  for (size_t i = 0U; i < 16U; ++i) {
    if (i)
      out += ',';
    out += num(m[i]);
  }
  out += "]}";
  return out;
}

} // namespace detail

// Builds a complete, minimal Scene 1.1 JSON oracle around the deterministic
// mesh-derived node/edge/surface arrays. The oracle is intentionally limited
// to a successful mesh-topology snapshot: features, regions, objects,
// constraints and issues are emitted as empty arrays and quality remains
// provisional. Production remains responsible for merging those independent
// runtime-owned layers and for preserving explicit failure issues.
inline Result assemble(const mesh_anchor_scene_entities::Result &entities,
                       const Input &input, const Options &options = {}) {
  Result out;
  if (entities.status != mesh_anchor_scene_entities::Status::kOk) {
    out.status = Status::kEntitiesUnavailable;
    return out;
  }
  if (!detail::canonical_uuid(input.project_id) || options.max_epochs == 0U ||
      options.max_json_bytes == 0U || options.max_epochs > kDefaultMaxEpochs ||
      options.max_json_bytes > kDefaultMaxJsonBytes) {
    out.status = Status::kInvalidArgument;
    return out;
  }
  if (!coordinate::is_valid_floor_frame(input.basis) ||
      !detail::bounded(input.basis.origin_m)) {
    out.status = Status::kInvalidBasis;
    return out;
  }
  if (input.epochs.size() > options.max_epochs) {
    out.status = Status::kResourceLimit;
    return out;
  }

  try {
    std::vector<size_t> order(input.epochs.size());
    for (size_t i = 0U; i < order.size(); ++i)
      order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      const auto &lhs = input.epochs[a];
      const auto &rhs = input.epochs[b];
      return std::tie(lhs.capture_id, lhs.session_epoch, lhs.transform_revision) <
             std::tie(rhs.capture_id, rhs.session_epoch, rhs.transform_revision);
    });

    for (size_t canonical = 0U; canonical < order.size(); ++canonical) {
      const auto &epoch = input.epochs[order[canonical]];
      if (!detail::valid_epoch(epoch)) {
        out.status = Status::kInvalidEpoch;
        return out;
      }
      if (canonical > 0U) {
        const auto &previous = input.epochs[order[canonical - 1U]];
        if (previous.capture_id == epoch.capture_id &&
            previous.session_epoch == epoch.session_epoch) {
          out.status = Status::kInvalidEpoch;
          return out;
        }
      }
    }

    std::string epochs_json = "[";
    for (size_t i = 0U; i < order.size(); ++i) {
      if (i)
        epochs_json += ',';
      epochs_json += detail::epoch_json(input.epochs[order[i]]);
    }
    epochs_json += ']';

    std::string scene =
        "{\"schema_version\":\"1.1\",\"project_id\":\"" + input.project_id +
        "\",\"scene_revision\":\"" + std::to_string(input.scene_revision) +
        "\",\"source_is_synthetic\":" +
        (input.source_is_synthetic ? "true" : "false") +
        ",\"units\":\"m\",\"basis\":{\"handedness\":\"right\",\"up\":\"+Y\","
        "\"floor_origin_m\":" + detail::vec(input.basis.origin_m) +
        ",\"floor_u\":" + detail::vec(input.basis.u) +
        ",\"floor_v\":" + detail::vec(input.basis.v) +
        ",\"floor_n\":" + detail::vec(input.basis.n) +
        "},\"epochs\":" + epochs_json + ",\"nodes\":" + entities.nodes_json +
        ",\"edges\":" + entities.edges_json + ",\"surfaces\":" +
        entities.surfaces_json +
        ",\"features\":[],\"regions\":[],\"objects\":[],\"constraints\":[],"
        "\"issues\":[],\"quality_state\":\"provisional\"}";

    size_t total = 0U;
    if (!detail::checked_add(0U, scene.size(), total) ||
        total > options.max_json_bytes) {
      out.status = Status::kResourceLimit;
      return out;
    }
    out.scene_json = std::move(scene);
    out.emitted_json_bytes = total;
    return out;
  } catch (const std::bad_alloc &) {
    out.status = Status::kResourceLimit;
    out.scene_json.clear();
    out.emitted_json_bytes = 0U;
    return out;
  }
}

} // namespace hss::mesh_anchor_scene_snapshot
