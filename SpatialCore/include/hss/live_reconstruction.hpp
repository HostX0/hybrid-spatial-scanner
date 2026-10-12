#pragma once
#include "hss/annotation_geometry.hpp"
#include "hss/bounded_ingress.hpp"
#include "hss/scene_editing.hpp"
#include <memory>
#include <string>
#include <vector>

namespace hss::live {
// Conservative reservations account for retained evidence, candidate scratch,
// immutable revisions and pinned snapshots. Allocator bookkeeping is covered
// by fixed per-entity allowances; these are not resident-memory measurements.
struct Budget {
  uint64_t limit{}, used{};
};
struct Lease {
  std::shared_ptr<Budget> budget;
  uint64_t bytes{};
  ~Lease() {
    if (budget)
      budget->used -= bytes;
  }
};
std::shared_ptr<Lease> reserve(const std::shared_ptr<Budget> &, uint64_t);
uint64_t event_reservation(const ingress::Event &);
struct Evidence {
  std::shared_ptr<Lease> lease;
  ingress::Event event;
};
// The retained immutable payload owns its reservation across source rebuilds
// and pinned snapshots; an unchanged rebuild copies only this shared handle.
struct UnknownOverlay {
  std::shared_ptr<Lease> lease;
  std::shared_ptr<const scene_editing::RetainedUnknowns> records;
  uint64_t serialized_bytes{};
};
struct Snapshot {
  std::shared_ptr<const UnknownOverlay> retained_unknowns;
  std::shared_ptr<Lease> lease;
  uint64_t revision{}, node_count{}, edge_count{}, issue_count{};
  std::vector<HSSVec3d> vertices;
  std::vector<uint32_t> triangles;
  std::string json;
  // Present only when the complete accepted runtime cut can be represented by
  // the B-031/B-032 contracts without repairing geometry or inventing lineage.
  std::optional<plan_semantics::PlanLayer> plan;
  std::optional<annotation_geometry::Layout> annotations;
};
class Reconstruction {
public:
  explicit Reconstruction(const HSSConfig &);
  ~Reconstruction();
  HSSStatus begin(std::shared_ptr<Evidence>);
  // One bounded quantum; the facade checks the steady-clock deadline between
  // calls. true means idle (either committed or failed), never partial publish.
  bool advance();
  bool busy() const;
  HSSStatus status() const;
  const std::shared_ptr<const Snapshot> &snapshot() const;
  std::shared_ptr<Budget> budget() const;
  HSSStatus set_synthetic(uint32_t);
  HSSStatus set_plan_reference(const HSSMat4d &, double);
  // Installs an already validated complete metric-scene candidate while
  // retaining the captured mesh buffers and source provenance of the current
  // immutable snapshot. The caller serializes this with source ingestion.
  HSSStatus publish_edited_scene(std::string candidate_scene_json,
                                 uint64_t expected_revision,
                                 uint64_t node_count, uint64_t edge_count,
                                 uint64_t issue_count,
                                 std::shared_ptr<const scene_editing::RetainedUnknowns>
                                     retained_unknowns = {});
  void seal_configuration();
  void cancel(HSSStatus);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace hss::live
