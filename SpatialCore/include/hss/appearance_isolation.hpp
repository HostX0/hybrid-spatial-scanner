#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace hss::appearance {

// This module intentionally accepts only an opaque target identifier and its
// immutable measured-scene revision. It neither receives nor returns vertices,
// dimensions, topology, transforms, or source mesh buffers.
struct MetricTargetRef {
  std::string entity_id;
  uint64_t measured_scene_revision{0U};
  bool operator==(const MetricTargetRef &) const = default;
};

// A missing verification is meaningful: it must not be treated as a positive
// result simply because an image exists.
enum class Verification : uint8_t {
  kUnknown,
  kVerified,
  kRejected,
};

// The candidate references a retained local visual asset. It contains visual
// evidence only; numeric confidence is visual quality, never metric accuracy.
struct KeyframeAppearance {
  std::string keyframe_id;
  std::string local_asset_id;
  uint64_t source_scene_revision{0U};
  Verification calibration{Verification::kUnknown};
  Verification accepted_transform{Verification::kUnknown};
  Verification visibility_consistency{Verification::kUnknown};
  double visual_confidence{0.0};
  bool operator==(const KeyframeAppearance &) const = default;
};

// The stored layer is an optional rendering association. The target revision is
// retained so a caller cannot use it to decorate a different measured revision.
struct AppearanceBinding {
  MetricTargetRef target;
  std::string keyframe_id;
  std::string local_asset_id;
  double visual_confidence{0.0};
  bool operator==(const AppearanceBinding &) const = default;
};

inline constexpr size_t kHardMaximumBindings = 16'384U;

struct Options {
  size_t maximum_bindings{1'024U};
};

enum class Status : uint8_t {
  kApplied,
  kUnknownCalibration,
  kRejectedCalibration,
  kUnknownTransform,
  kRejectedTransform,
  kUnknownVisibility,
  kRejectedVisibility,
  kStaleSceneRevision,
  kInvalidInput,
  kInvalidOptions,
  kResourceLimit,
  kInternalFailure,
};

struct UpdateResult {
  Status status{Status::kInvalidOptions};
  // Always false. Texture admission is deliberately incapable of changing the
  // measured scene; consumers can display this invariant in an audit trail.
  bool metric_geometry_changed{false};
  // A rejected, unknown, stale, or resource-limited candidate leaves an
  // already admitted visual binding intact.
  bool retained_existing_binding{false};
  std::optional<AppearanceBinding> active_binding;
};

// Owns only optional appearance associations. It has no dependencies on the
// reconstruction, metric scene, topology, or rendering implementation.
class AppearanceIsolation {
public:
  explicit AppearanceIsolation(Options options = {});
  ~AppearanceIsolation();

  [[nodiscard]] bool valid() const;
  [[nodiscard]] size_t binding_count() const;
  [[nodiscard]] std::optional<AppearanceBinding>
  binding_for(const MetricTargetRef &target) const;

  // Applies only a fully verified, same-revision visual candidate. Failures are
  // atomic with respect to the previously admitted appearance layer.
  [[nodiscard]] UpdateResult submit(const MetricTargetRef &target,
                                    const KeyframeAppearance &candidate);

private:
  struct Impl;
  Options options_;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] const char *status_name(Status status);

} // namespace hss::appearance
