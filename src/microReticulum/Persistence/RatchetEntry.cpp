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

#include "RatchetEntry.h"

#include <MsgPack.h>

using namespace RNS;
using namespace RNS::Persistence;

// Encodes a RatchetEntry as a 2-element MsgPack array:
//   [received, ratchet]
//
// New fields are appended by bumping the array length; old decoders see the
// extra elements as trailing array members they can ignore.
/*static*/ std::vector<uint8_t> microStore::Codec<RatchetEntry>::encode(const RatchetEntry& entry) {

	if (!entry) return {};

	MsgPack::Packer p;
	p.packArraySize(2);

	// received
	p.packFloat64(entry._received);

	// ratchet
	p.packBinary(entry._ratchet.data(), entry._ratchet.size());

	return std::vector<uint8_t>(p.data(), p.data() + p.size());
}

/*static*/ bool microStore::Codec<RatchetEntry>::decode(const std::vector<uint8_t>& data, RatchetEntry& entry) {
	if (data.empty()) return false;

	MsgPack::Unpacker u;
	u.feed(data.data(), data.size());

	if (!u.isArray()) return false;
	const size_t n = u.unpackArraySize();
	if (n < 2) return false;

	// received
	if (!u.deserialize(entry._received)) return false;

	// ratchet
	{
		MsgPack::bin_t<uint8_t> b;
		if (!u.deserialize(b)) return false;
		entry._ratchet = Bytes(b.data(), b.size());
	}

	return true;
}
