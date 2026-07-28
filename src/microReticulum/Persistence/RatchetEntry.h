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

#if defined(RNS_USE_FS) && RNS_PERSIST_KNOWN_RATCHETS
#include <microStore/FileStore.h>
#else
#include <microStore/HeapStore.h>
#endif
#include <microStore/TypedStore.h>
#include <microStore/Codec.h>

namespace RNS { namespace Persistence {

// Identity-side remote ratchet cache entry: the most recently announced ratchet
// PUBLIC key for a given remote destination hash, and when it was received.
// Mirrors Python RNS's Identity.known_ratchets / _remember_ratchet on-disk format.
class RatchetEntry {
public:
	RatchetEntry() {}
	RatchetEntry(double received, const RNS::Bytes& ratchet) :
		_received(received),
		_ratchet(ratchet)
	{
	}
	inline explicit operator bool() const {
		return _ratchet.size() > 0;
	}
	inline bool operator < (const RatchetEntry& entry) const {
		return _received < entry._received;
	}
public:
	double _received = 0;
	RNS::Bytes _ratchet;
public:
#ifndef NDEBUG
	inline std::string debugString() const {
		return "RatchetEntry: received=" + std::to_string(_received) +
			" ratchet=" + _ratchet.toHex();
	}
#endif
};

#if defined(RNS_USE_FS) && RNS_PERSIST_KNOWN_RATCHETS
using RatchetStore = microStore::BasicFileStore<Utilities::Memory::ContainerAllocator<uint8_t>>;
#else
using RatchetStore = microStore::BasicHeapStore<Utilities::Memory::ContainerAllocator<uint8_t>>;
#endif
using KnownRatchets = microStore::TypedStore<Bytes, RatchetEntry, RatchetStore>;

} }

namespace microStore {
template<>
struct Codec<RNS::Persistence::RatchetEntry>
{
	static std::vector<uint8_t> encode(const RNS::Persistence::RatchetEntry& entry);
	static bool decode(const std::vector<uint8_t>& data, RNS::Persistence::RatchetEntry& entry);
};
}
