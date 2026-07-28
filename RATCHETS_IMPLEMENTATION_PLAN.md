# Implementing Ratchets in microReticulum — Plan for Parity with Python RNS

**Status: implemented.** Originally written as a design/scoping doc based on reading the
Python RNS 1.4.2 reference implementation (`RNS/Identity.py`, `RNS/Destination.py`)
alongside the current C++ port's `Identity.cpp`/`.h` and `Destination.cpp`/`.h`. Left
in place as a reference for the design rationale; see the Known Follow-up at the bottom
for the one deliberately deferred piece.

## What "Ratchets" actually is (two distinct mechanisms)

It's easy to think of ratchets as one feature. In upstream RNS it's two cooperating
halves, owned by different classes:

### A. Owned-destination ratchet history (`Destination.py`) — for *receiving*

A destination that enables ratchets keeps a rotating list of its own X25519 keypairs
(private keys retained locally, public key of the newest one announced to the network).
When a peer sends it a packet, they encrypt against whichever ratchet public key they
last saw announced. Since packets can arrive after the destination has already rotated,
the destination retains a history of old private ratchets and tries each one on decrypt
— this is what gives forward secrecy without breaking delivery of in-flight data.

Key pieces (`Destination.py`):
- `RATCHET_COUNT = 512` — how many past ratchets to retain for decrypt attempts
- `RATCHET_INTERVAL = 30*60` — minimum seconds between rotations
- `enable_ratchets(path)` — turns the feature on, loads or creates a persisted list at `path`
- `rotate_ratchets()` — generates a new keypair via `Identity._generate_ratchet()`, inserts
  at the front of `self.ratchets`, persists
- `_clean_ratchets()` — truncates the list to `retained_ratchets`
- `_persist_ratchets()` / `_reload_ratchets()` — signs and writes the list atomically
  (tmp file + rename), verifies signature on reload
- `enforce_ratchets()` — reject any packet not encrypted against a ratchet key
- Announce integration: the newest ratchet's **public** bytes get appended to the signed
  announce payload (`Destination.py:287-302`)
- Decrypt integration: `Destination.decrypt()` passes `self.ratchets` (the whole private
  list) into `Identity.decrypt(..., ratchets=self.ratchets, ...)`, which tries each one

### B. Remote ratchet cache (`Identity.py`) — for *sending*

Separately, every `Identity` instance remembers the most recently announced ratchet
**public** key for every remote destination it has seen announce one, so it can encrypt
outbound packets to that destination using the ratchet key instead of the long-term
static identity key.

Key pieces (`Identity.py`):
- `known_ratchets = {}` — class-level dict, `destination_hash -> ratchet_pubkey_bytes`,
  **only one** remembered per remote destination (not a history — this side doesn't need one)
- `RATCHET_EXPIRY = 60*60*24*30` (30 days) — if no newer ratchet has been announced
  within this window, the remembered one is discarded
- `_generate_ratchet()` / `_ratchet_public_bytes()` / `_get_ratchet_id()` — keypair
  generation and ID derivation (hash of pubkey, truncated to `NAME_HASH_LENGTH`)
- `_remember_ratchet(destination_hash, ratchet)` — stores in memory + persists to
  `storagepath/ratchets/<hexhash>` as `{"ratchet": ..., "received": time.time()}`
- `_clean_ratchets()` — background job scanning the ratchet directory, deleting anything
  past `RATCHET_EXPIRY`
- `get_ratchet(destination_hash)` / `current_ratchet_id(destination_hash)` — lazy-load
  from disk into the in-memory cache if not already there
- `validate_announce()` (the code already ported in `Identity.cpp:327-365`) parses the
  ratchet bytes out of the announce and includes them in the signed data — but upstream
  additionally calls `_remember_ratchet(destination_hash, ratchet)` after the signature
  validates (`Identity.py:595`). **This call is the one thing missing from the otherwise-ported
  parsing logic** — the rest of side B doesn't exist at all.
- `Identity.encrypt(plaintext, ratchet=None)` — if a ratchet pubkey is supplied, ECDH
  against that instead of the identity's static pubkey
- `Identity.decrypt(ciphertext, ratchets=None, enforce_ratchets=False, ratchet_id_receiver=None)`
  — tries each of the supplied (own, historical) private ratchets before falling back to
  the static private key

## Current C++ state

- `Type.h:231` — `RATCHETSIZE = 256` (bits) already defined, matches upstream
- `Type.h:238` — `RATCHET_EXPIRY = 60*60*24*30` already defined, matches upstream, but
  **unused anywhere** — nothing reads it yet
- `Identity.cpp:327-365` — wire-format parsing of an inbound ratchet from an announce
  packet is implemented (extracting the ratchet bytes at the right offset, including them
  in the signed-data hash for signature validation) — this part is correctly ported
- Everything else in both mechanisms A and B is absent:
  - No `Identity::known_ratchets` map, no `_generate_ratchet`/`_ratchet_public_bytes`/
    `_get_ratchet_id`/`_remember_ratchet`/`get_ratchet`/`current_ratchet_id`/`_clean_ratchets`
  - `Identity::encrypt()` / `Identity::decrypt()` (`Identity.h:105-106`) take no ratchet
    parameter at all — the plumbing for "encrypt against this pubkey instead" and "try
    this list of private keys" doesn't exist
  - No `Destination::ratchets` member, no `enable_ratchets`/`rotate_ratchets`/
    `enforce_ratchets`/`set_retained_ratchets`/`set_ratchet_interval`, no persistence
  - `Destination::encrypt()`/`decrypt()` (`Destination.cpp:453,490`) call straight through
    to `Identity::encrypt`/`decrypt` with no ratchet argument

## What needs to be built

### 1. Crypto plumbing (prerequisite for everything else)

- `Identity::encrypt(const Bytes& plaintext, const Bytes& ratchet = {})` — when `ratchet`
  is non-empty, ECDH against `X25519PublicKey::from(ratchet)` instead of `_object->_pub_bytes`
- `Identity::decrypt(const Bytes& ciphertext_token, const std::vector<Bytes>& ratchets = {}, bool enforce_ratchets = false, /* ratchet_id_receiver mechanism */)`
  — try each ratchet private key in order, matching upstream's try/except-per-candidate
  loop; fall back to the static private key unless `enforce_ratchets` is set
  - Upstream's `ratchet_id_receiver` is a callback-ish "tell the caller which ratchet ID
    ended up being used" — in C++ this is probably cleanest as an optional out-parameter
    (`Bytes* used_ratchet_id = nullptr`) rather than trying to port Python's duck-typed
    attribute-setting pattern
- These are self-contained changes to `Identity.cpp`/`.h`; nothing else depends on them
  existing but not yet being *used*, so this can be built and unit-tested against the
  existing crypto test harness before touching `Destination`/announce logic

### 2. Identity-side remote ratchet cache (mechanism B)

- `Identity::_generate_ratchet()`, `_ratchet_public_bytes()`, `_get_ratchet_id()` — thin
  wrappers around the existing X25519 keypair primitives already used elsewhere in
  `Identity.cpp` for the main identity keypair; no new crypto, just reuse
- A new `Persistence::RatchetEntry` following the exact pattern already established by
  `Persistence::IdentityEntry` / `Persistence::DestinationEntry` (see
  `src/microReticulum/Persistence/IdentityEntry.h`, `DestinationEntry.h`) — microStore-backed,
  keyed by destination hash, storing `{ratchet_pubkey, received_timestamp}`
- `Identity::_remember_ratchet()` / `get_ratchet()` / `current_ratchet_id()` as static
  methods backed by the new `RatchetEntry` store, mirroring how `known_destinations` /
  `known_identities` are already handled elsewhere in this port (same keyed-store idiom)
- `_clean_ratchets()` as a periodic job — check what job-scheduling mechanism the rest of
  the port uses for similar periodic maintenance (there should be an existing pattern from
  e.g. path-table cleanup in `Transport.cpp`) rather than inventing a new one
- Wire `_remember_ratchet(destination_hash, ratchet)` into the existing announce-validation
  path at `Identity.cpp:365` (after signature validation succeeds) — this is the one-line
  gap in otherwise-ported logic mentioned above

### 3. Destination-side owned ratchet history (mechanism A)

- Add `_ratchets` (a `std::vector<Bytes>`, private keys, newest first), `_ratchets_path`,
  `_ratchet_interval`, `_retained_ratchets`, `_latest_ratchet_time`, `_latest_ratchet_id`,
  `_enforce_ratchets` to `DestinationData` (mirrors `LinkData.h`'s pattern of pImpl-style
  data members)
- `Destination::enable_ratchets(path)`, `rotate_ratchets()`, `_clean_ratchets()`,
  `_persist_ratchets()`, `_reload_ratchets()`, `enforce_ratchets()`,
  `set_retained_ratchets()`, `set_ratchet_interval()` — direct ports of the upstream logic,
  using the same `RatchetEntry`/microStore persistence primitive from step 2 rather than a
  separate ad-hoc file format
- Wire into `Destination::announce()` — append the newest ratchet's public bytes to the
  announce payload and include in the signed data (the C++ announce-building code already
  has a slot for this since `Identity.cpp` already parses it on the receive side — check
  whether `Destination.cpp`'s announce construction has a corresponding gap to fill, or
  whether it's a new addition)
- Wire into `Destination::decrypt()` — pass `_ratchets` through to the new
  `Identity::decrypt()` ratchets parameter from step 1

### 4. Constants and defaults

- Add `Destination::RATCHET_COUNT = 512` and `RATCHET_INTERVAL = 30*60` to match upstream
  defaults — but **reconsider the retained-count default for embedded targets**. 512
  retained ratchets × 32 bytes (X25519 private key size) = 16 KB per ratchet-enabled
  destination, which collides directly with the flash constraints this project's own
  README already flags for nRF52840 boards (28 KB total FS on some boards, per the "Known
  Limitations" section). Recommend a smaller compile-time or runtime-configurable default
  for constrained targets, with 512 remaining available for native/desktop builds.

## Suggested build order

1. Crypto plumbing (`Identity::encrypt`/`decrypt` ratchet parameters) — isolated, testable
   standalone
2. Identity-side remote ratchet cache + the one-line wire-up into existing announce
   validation — completes mechanism B, and is useful on its own even before mechanism A
   exists (a node can start remembering peers' ratchets before it has any of its own)
3. `Persistence::RatchetEntry` storage type
4. Destination-side owned ratchet history (mechanism A) — depends on 1 and 3
5. Interop testing — this project already has a Python-interop test pattern
   (`link_interop_sender`, `resource_interop_sender`, `packet_interop_sender` per the
   README's example/test list); a `ratchet_interop_sender`/receiver pair against the
   Python reference implementation would be the most convincing parity check, since
   ratchets only matter cross-implementation if a C++ node and Python node can each
   decrypt what the other encrypted using an announced ratchet key.

## Open questions to resolve before starting

- What periodic-job mechanism should `_clean_ratchets()` (both the Identity-side and
  Destination-side variants) hook into? Needs to match whatever pattern already exists
  for other maintenance jobs in this codebase (e.g. path-table cleanup), rather than
  introducing a new one — **and note that this ties into the Link-watchdog work already
  proposed in [RNS_PROPOSED_CHANGES.md](RNS_PROPOSED_CHANGES.md)**: if that job-scheduling
  primitive doesn't exist yet, either work item may need to introduce it, so these two
  pieces of work should be coordinated rather than done in isolation.
- Embedded memory budget for retained ratchets (see constants section above) — worth a
  decision up front rather than defaulting to the desktop-appropriate 512 and discovering
  the flash problem later.
- Whether `enforce_ratchets` and the `ratchet_id_receiver` out-parameter are needed for a
  first cut, or can be deferred — they're used by upstream mainly for stricter security
  postures and diagnostics, not required for basic interop.

## Known follow-up: owned ratchet history doesn't survive a restart

`DestinationRatchetsEntry` (the owned-history side, mechanism A) is deliberately backed
by `BasicHeapStore`, not the flash-backed `FileStore` that `RatchetEntry` (the remote
cache, mechanism B) conditionally uses. This means a device restart loses a destination's
own past ratchet private keys.

Impact assessed as low-to-moderate and scenario-dependent, not a correctness bug in the
general case:

- **Remote cache (mechanism B) not surviving a restart**: doesn't matter. Self-heals as
  soon as the next announce is heard from each peer; the only cost is a brief window
  falling back to static-key encryption.
- **Owned history (mechanism A) not surviving a restart**: matters only for packets a
  peer encrypted against one of *this device's* pre-restart ratchets that arrive (or are
  still in flight) after the restart — those become permanently undecryptable, since the
  private key no longer exists anywhere. Future traffic is unaffected (a fresh ratchet
  is generated on the next announce). For a node that stays up for long stretches
  (typical desktop/server transport node), this is a rare edge case. For a device that
  restarts often (embedded target on flaky power, a mobile app getting killed and
  relaunched), it's a real, recurring gap.

Fix, if ever needed: swap `DestinationRatchetsStore` in
`Persistence/DestinationRatchetsEntry.h` from `BasicHeapStore` to the flash-backed
`FileStore`, gated behind a build flag the same way `RatchetEntry.h` already gates its
own optional flash persistence (`RNS_USE_FS && RNS_PERSIST_KNOWN_RATCHETS` — note that
flag isn't registered as a CMake option yet either, so both would need wiring up
together). Not implemented now; revisit if a concrete embedded/flaky-power target needs
ratchet-enabled destinations to survive reboot without data loss.
