# Wionode Report Delivery Failure

## Symptom

After fixing the two Reticulum-level divergences (see
[Neighbour Probing Wonkiness.md](Neighbour%20Probing%20Wonkiness.md)),
announces from wionode started propagating to other nodes correctly, but
wionode still consistently failed to deliver reports to the central
server. The node was also observed to "go dead" at some point during
extended runtime, raising suspicion of memory exhaustion (asked about as
"stack overflow / unbounded arrays").

## Root cause: two structurally-linked bugs

### 1. `path_request_timeout_seconds` too short (15s default)

`RnsNode::send()` deadlines a send at `path_request_timeout_seconds`
after issuing a `PATH_REQUEST` (`cpp/meshpack-shared-cpp/src/RnsNode.cpp`,
`RnsNode::send()`). The old default, 15.0s
(`cpp/outpost-cpp/include/outpost/Config.h`), only bounds path/identity
resolution — but LoRa's per-hop airtime and duty-cycle delay over a
multi-hop path to the server can easily exceed that on wionode's direct
half-duplex LoRa link. Every attempt was plausibly timing out before a
path was ever found, which is why delivery was *consistently* (not
intermittently) failing.

### 2. Timing out a send leaked its `LXMF::OutboundMessage` forever

When `RnsNode::loop()` gave up on a send whose `PATH_REQUESTED` deadline
passed, it stopped watching the send but never told the underlying
`LXMF::OutboundMessage` it had failed — the code comment said outright:
*"there's no cancel API in lxmf-cpp yet); we just stop waiting on it
here"* (`RnsNode.cpp`, the `PATH_REQUESTED && now > it->deadline` branch).

`LXMRouter::_pending` (`cpp/external/lxmf-cpp/src/LXMRouter.cpp`,
`LXMRouter::loop()`) only ever prunes an entry once it reaches
`DELIVERED` or `FAILED`. A message stuck in `PATH_REQUESTED` — which,
given bug #1, was *every* report to the server — stayed in `_pending`
permanently, still holding its fully packed payload
(`OutboundMessage::_packed`). Every failed report-send leaked memory.
With the server chronically unreachable (from this bug plus the earlier
neighbor-probing/path-blocking bugs), this was a slow, steady, unbounded
heap leak on a no-PSRAM ESP32-S3 — the most plausible explanation for
wionode eventually going dead.

Other memory-safety angles were checked and ruled out: `Transport.cpp`'s
announce/path/reverse/link/discovery tables are all genuinely bounded
with real periodic cull logic; `History.h`'s buffers are explicitly
capacity-limited; the LoRa driver's stack buffers are small fixed
255-byte arrays, not unbounded; no stack-size override is configured
(default ESP32-S3 Arduino loopTask stack, ~8KB) but no recursion was
found in the packet/msgpack parsing paths that could plausibly blow it
from mesh-supplied data. This was a heap leak, not a stack overflow.

## Fix applied

1. **Raised the default timeout** from 15.0s to 60.0s
   (`cpp/outpost-cpp/include/outpost/Config.h`,
   `cpp/outpost-cpp/src/Config.cpp`) — still overridable per-node via
   `MESHPACK_PATH_REQUEST_TIMEOUT_SECONDS` if a specific deployment's
   link characteristics need something else. Applies to both wionode
   (ESP32) and pinodeHamCPP (Pi/RNode) since they share the same
   `outpost-cpp` binary; a longer wait before giving up is harmless for
   the faster RNode-mediated link.
2. **Fixed the leak**: `RnsNode.cpp`'s `PATH_REQUESTED`-timeout branch
   now calls `it->outbound->internal_set_state(LXMF::OutboundMessage::State::FAILED)`
   before detaching, so `LXMRouter::loop()` prunes the entry (and fires
   its failed-callback, a no-op here) on its next pass instead of
   holding it forever.

Verified: `build-native` and `build-rpi` (`meshpack-outpost-lib`)
compile cleanly, and the existing `test_config`, `test_rns_bridge`,
`test_neighbours`, and `test_transport` suites all still pass.

## Diagnostics suggested but not yet added

If reports still fail after this fix, or to confirm the leak
theory/measure real path-resolution latency before tuning the timeout
further:

- Log `LXMRouter::_pending.size()` / `RnsNode::_pending_sends.size()`
  periodically and watch for monotonic growth over a live run.
- Log the actual time from `RNS::Transport::request_path()` to
  `has_path()` becoming true for the reporting destination, to see how
  close to (or over) 60s real path resolution takes on the current
  topology.
</content>
</invoke>
