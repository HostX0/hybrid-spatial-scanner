#include "hss/bounded_ingress.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using hss::ingress::Admission;
using hss::ingress::BoundedIngress;
using hss::ingress::Event;
using hss::ingress::EventKind;

void check(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

Event event(EventKind kind, uint64_t sequence, uint64_t payload_bytes) {
  Event result{};
  result.kind = kind;
  result.event_sequence = sequence;
  result.payload_bytes = payload_bytes;
  result.payload.assign(static_cast<size_t>(payload_bytes), 0x5aU);
  return result;
}

}  // namespace

int main() {
  BoundedIngress ingress(8U, 2U);
  check(ingress.admission(EventKind::frame, 4U) == Admission::accepted,
        "first frame fits both byte and count limits");
  check(ingress.enqueue(event(EventKind::frame, 10U, 4U)) == Admission::accepted,
        "first frame enqueued");
  check(ingress.enqueue(event(EventKind::frame, 11U, 4U)) == Admission::accepted,
        "second frame enqueued");
  check(ingress.admission(EventKind::frame, 1U) == Admission::dropped_frame,
        "expendable frame is dropped before allocating its payload");
  ingress.record_dropped_frame();
  check(ingress.dropped_frames() == 1U && ingress.pending_frames() == 2U,
        "dropped frame counter and pending count are distinct");

  Event removal = event(EventKind::mesh_remove, 12U, 0U);
  removal.anchor_id.bytes[0] = 0x7fU;
  removal.anchor_revision = 9U;
  check(ingress.enqueue(std::move(removal)) == Admission::accepted,
        "zero-byte removal survives full frame mailbox");
  Event next{};
  check(ingress.pop(&next) && next.kind == EventKind::frame && next.event_sequence == 10U,
        "FIFO retains first frame before later controls");
  check(ingress.pop(&next) && next.kind == EventKind::frame && next.event_sequence == 11U,
        "FIFO retains second frame before later controls");
  check(ingress.pop(&next) && next.kind == EventKind::mesh_remove && next.event_sequence == 12U &&
            next.anchor_id.bytes[0] == 0x7fU && next.anchor_revision == 9U,
        "removal identity/order is retained");
  check(ingress.working_bytes() == 0U && ingress.pending_frames() == 0U,
        "drain releases all byte and frame accounting");

  BoundedIngress stalled(4U, 2U);
  check(stalled.enqueue(event(EventKind::frame, 20U, 4U)) == Admission::accepted,
        "stalled consumer setup");
  check(stalled.admission(EventKind::command, 1U) == Admission::resource_limit,
        "command pressure is explicit instead of a silent drop");
  check(stalled.dropped_frames() == 0U, "control backpressure does not affect frame counter");
  check(stalled.pop(&next), "consumer drains stalled frame");
  check(stalled.enqueue(event(EventKind::command, 21U, 1U)) == Admission::accepted,
        "command can retry after drain");
  stalled.close();
  check(stalled.admission(EventKind::mesh_replace, 0U) == Admission::closed,
        "closed ingress rejects new structural events explicitly");
  check(stalled.pop(&next) && next.kind == EventKind::command,
        "close preserves already accepted work for drain");
  check(!stalled.pop(&next), "closed and drained ingress is empty");

  BoundedIngress lifetime(1U, 1U);
  uint8_t caller_byte = 0x31U;
  Event borrowed_metadata = event(EventKind::frame, 22U, 1U);
  borrowed_metadata.frame.depth_bytes = &caller_byte;
  check(lifetime.enqueue(std::move(borrowed_metadata)) == Admission::accepted,
        "owned payload setup");
  caller_byte = 0U;
  check(lifetime.pop(&next) && next.frame.depth_bytes == nullptr && next.payload[0] == 0x5aU,
        "queued metadata retains no caller pointer after enqueue");

  BoundedIngress zero_byte_controls(1U, 1U);
  for (uint32_t index = 0; index < BoundedIngress::kMaxPendingEvents; ++index) {
    check(zero_byte_controls.enqueue(event(EventKind::epoch, index, 0U)) == Admission::accepted,
          "finite event-count bound accepts capacity");
  }
  check(zero_byte_controls.admission(EventKind::epoch, 0U) == Admission::resource_limit,
        "zero-byte controls cannot form an unbounded queue");

  std::cout << "bounded ingress overload, FIFO, close/drain, and retry checks passed\n";
  return EXIT_SUCCESS;
}
