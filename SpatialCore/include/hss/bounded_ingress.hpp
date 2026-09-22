#ifndef HSS_BOUNDED_INGRESS_HPP
#define HSS_BOUNDED_INGRESS_HPP

#include "hss/hss_api.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace hss::ingress {

// This is an owner-serialized mailbox.  It intentionally creates no worker
// thread; hss_engine_step is the only consumer.  Payload pointers in Event
// metadata are always null: the byte vector is the sole retained ownership.
enum class EventKind : uint8_t {
  frame,
  epoch,
  mesh_add,
  mesh_replace,
  mesh_remove,
  room_proposal,
  command,
};

struct Event {
  EventKind kind{};
  // Exactly one of these is populated for the corresponding typed event.
  // Any pointer fields are nulled before enqueue; consumers bind them to
  // payload only while processing the popped owned event.
  HSSFrame frame{};
  HSSEpoch epoch{};
  HSSMeshEvent mesh{};
  HSSId capture_id{};
  HSSId anchor_id{};
  uint64_t event_sequence{};
  uint64_t anchor_revision{};
  uint32_t session_epoch{};
  uint64_t payload_bytes{};
  std::vector<uint8_t> payload;
};

enum class Admission : uint8_t {
  accepted,
  dropped_frame,
  resource_limit,
  closed,
};

class BoundedIngress {
 public:
  // A finite count bound matters even for zero-byte removals/epochs.  The
  // byte bound is supplied by HSSConfig; control events use this separate
  // fixed ceiling because the v1 ABI has no control-count configuration.
  static constexpr uint32_t kMaxPendingEvents = 128U;

  BoundedIngress(uint64_t max_bytes, uint32_t max_frames);

  [[nodiscard]] Admission admission(EventKind kind, uint64_t payload_bytes) const;
  void record_dropped_frame();
  // The caller must obtain an accepted admission before allocating/copying the
  // payload.  enqueue rechecks bounds so a future non-serialized caller still
  // gets a status rather than silently growing this queue.
  [[nodiscard]] Admission enqueue(Event&& event);
  [[nodiscard]] bool pop(Event* out);
  void close();

  [[nodiscard]] uint64_t working_bytes() const { return working_bytes_; }
  [[nodiscard]] uint64_t pending_frames() const { return pending_frames_; }
  [[nodiscard]] uint64_t dropped_frames() const { return dropped_frames_; }
  [[nodiscard]] uint64_t pending_events() const { return events_.size(); }
  [[nodiscard]] bool closed() const { return closed_; }

 private:
  [[nodiscard]] static bool is_frame(EventKind kind);
  [[nodiscard]] bool fits_bytes(uint64_t payload_bytes) const;

  uint64_t max_bytes_;
  uint32_t max_frames_;
  uint64_t working_bytes_{0};
  uint64_t pending_frames_{0};
  uint64_t dropped_frames_{0};
  bool closed_{false};
  std::deque<Event> events_;
};

}  // namespace hss::ingress

#endif
