/*
 * Copyright (c) 2026 Chad Attermann
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */

#pragma once

#include "../Bytes.h"
#include "../Type.h"
#include "../Utilities/Memory.h"

#include <microStore/HeapStore.h>
#include <microStore/TypedStore.h>
#include <microStore/Codec.h>

#include <vector>

namespace RNS { namespace Persistence {

// Destination-side owned ratchet history: the rotating list of a destination's own
// X25519 PRIVATE ratchet keys (newest first), retained so packets encrypted against
// an older, already-rotated-past ratchet can still be decrypted. Mirrors Python RNS's
// Destination.ratchets / _persist_ratchets on-disk format (there, a signed file per
// destination; here, an entry in a keyed store, keyed by owning destination hash).
//
// Kept always on the heap store (not the flash-backed FileStore used for the
// remote-ratchet cache in RatchetEntry.h): this data changes far more often
// (every rotation) and the number of ratchet-enabled *owned* destinations on a
// single node is expected to be small, so the flash-wear tradeoff isn't worth it
// by default. Apps needing persistence across reboots should reload/re-announce
// on startup, same as upstream recommends when ratchet files are lost.
class DestinationRatchetsEntry {
public:
	DestinationRatchetsEntry() {}
	DestinationRatchetsEntry(const std::vector<RNS::Bytes>& ratchets, double latest_ratchet_time) :
		_ratchets(ratchets),
		_latest_ratchet_time(latest_ratchet_time)
	{
	}
	inline explicit operator bool() const {
		return _ratchets.size() > 0;
	}
public:
	std::vector<RNS::Bytes> _ratchets;
	double _latest_ratchet_time = 0;
};

using DestinationRatchetsStore = microStore::BasicHeapStore<Utilities::Memory::ContainerAllocator<uint8_t>>;
using DestinationRatchets = microStore::TypedStore<Bytes, DestinationRatchetsEntry, DestinationRatchetsStore>;

} }

namespace microStore {
template<>
struct Codec<RNS::Persistence::DestinationRatchetsEntry>
{
	static std::vector<uint8_t> encode(const RNS::Persistence::DestinationRatchetsEntry& entry);
	static bool decode(const std::vector<uint8_t>& data, RNS::Persistence::DestinationRatchetsEntry& entry);
};
}
