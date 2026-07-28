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

#include "DestinationRatchetsEntry.h"

#include <MsgPack.h>

using namespace RNS;
using namespace RNS::Persistence;

// Encodes a DestinationRatchetsEntry as a 2-element MsgPack array:
//   [ratchets (array of binary), latest_ratchet_time]
/*static*/ std::vector<uint8_t> microStore::Codec<DestinationRatchetsEntry>::encode(const DestinationRatchetsEntry& entry) {

	if (!entry) return {};

	MsgPack::Packer p;
	p.packArraySize(2);

	// ratchets
	p.packArraySize(entry._ratchets.size());
	for (const Bytes& ratchet : entry._ratchets) {
		p.packBinary(ratchet.data(), ratchet.size());
	}

	// latest_ratchet_time
	p.packFloat64(entry._latest_ratchet_time);

	return std::vector<uint8_t>(p.data(), p.data() + p.size());
}

/*static*/ bool microStore::Codec<DestinationRatchetsEntry>::decode(const std::vector<uint8_t>& data, DestinationRatchetsEntry& entry) {
	if (data.empty()) return false;

	MsgPack::Unpacker u;
	u.feed(data.data(), data.size());

	if (!u.isArray()) return false;
	const size_t n = u.unpackArraySize();
	if (n < 2) return false;

	// ratchets
	{
		if (!u.isArray()) return false;
		const size_t count = u.unpackArraySize();
		entry._ratchets.clear();
		entry._ratchets.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			MsgPack::bin_t<uint8_t> b;
			if (!u.deserialize(b)) return false;
			entry._ratchets.emplace_back(b.data(), b.size());
		}
	}

	// latest_ratchet_time
	if (!u.deserialize(entry._latest_ratchet_time)) return false;

	return true;
}
