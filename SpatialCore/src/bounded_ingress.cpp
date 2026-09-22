#include "hss/bounded_ingress.hpp"

#include <utility>

namespace hss::ingress {

BoundedIngress::BoundedIngress(uint64_t max_bytes, uint32_t max_frames)
    : max_bytes_(max_bytes), max_frames_(max_frames) {}

bool BoundedIngress::is_frame(EventKind kind) {
  return kind == EventKind::frame;
}

bool BoundedIngress::fits_bytes(uint64_t payload_bytes) const {
  return payload_bytes <= max_bytes_ && working_bytes_ <= max_bytes_ - payload_bytes;
}

Admission BoundedIngress::admission(EventKind kind, uint64_t payload_bytes) const {
  if (closed_) {
    return Admission::closed;
  }
  if (is_frame(kind) && pending_frames_ >= max_frames_) {
    return Admission::dropped_frame;
  }
  if (events_.size() >= kMaxPendingEvents || !fits_bytes(payload_bytes)) {
    return Admission::resource_limit;
  }
  return Admission::accepted;
}

void BoundedIngress::record_dropped_frame() {
  ++dropped_frames_;
}

Admission BoundedIngress::enqueue(Event&& event) {
  const Admission result = admission(event.kind, event.payload_bytes);
  if (result == Admission::dropped_frame) {
    record_dropped_frame();
    return result;
  }
  if (result != Admission::accepted) {
    return result;
  }
  // The engine calls are serialized by the ABI owner.  Still validate the
  // event's declared size against the owned data before it becomes visible.
  if (event.payload.size() != event.payload_bytes) {
    return Admission::resource_limit;
  }
  // Do not permit an internal caller to turn this bounded owned queue into a
  // borrowed-pointer queue.  A future consumer may bind local pointers to the
  // vector after pop, but no C pointer survives enqueue.
  event.frame.depth_bytes = nullptr;
  event.frame.confidence_bytes = nullptr;
  event.mesh.vertices_xyz = nullptr;
  event.mesh.triangle_indices = nullptr;
  const bool frame = is_frame(event.kind);
  const uint64_t payload_bytes = event.payload_bytes;
  events_.push_back(std::move(event));
  working_bytes_ += payload_bytes;
  if (frame) {
    ++pending_frames_;
  }
  return Admission::accepted;
}

bool BoundedIngress::pop(Event* out) {
  if (out == nullptr || events_.empty()) {
    return false;
  }
  Event event = std::move(events_.front());
  events_.pop_front();
  working_bytes_ -= event.payload_bytes;
  if (is_frame(event.kind)) {
    --pending_frames_;
  }
  *out = std::move(event);
  return true;
}

void BoundedIngress::close() {
  closed_ = true;
}

}  // namespace hss::ingress
