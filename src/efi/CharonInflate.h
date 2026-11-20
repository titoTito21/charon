// SPDX-License-Identifier: GPL-2.0-or-later
// Charon: streaming raw DEFLATE (RFC 1951) decoder. Input is pulled in pieces
// through a callback, output is pushed in pieces through a callback, so a
// stream of any length needs only the caller's buffer as window.
// Pure logic: no allocation, the caller supplies every buffer.
//
// Two ways to run it:
//  - streaming: pass an Output and a buffer of at least kWindowBytes. The
//    buffer is the sliding window; each time it fills up (and once at the end)
//    its content is handed to Output::write.
//  - one shot: pass no Output. Everything is decoded into the buffer, which
//    must hold the whole result; more data than that is Error::Overflow (the
//    buffer is then full and *produced == capacity).
//
// A Decoder is about 4 KiB: keep it in static or caller-owned memory, not on
// the stack. It holds no state between Run calls.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace hakefi::inflate {

constexpr size_t kWindowBytes = 32768;

enum class Error : uint32_t {
	None,
	BadArgument, // null buffer, or a streaming buffer below kWindowBytes
	Truncated,   // the input ended before the final block did
	Corrupt,     // not a valid DEFLATE stream
	Overflow,    // one shot: the result does not fit the buffer
	Sink,        // Output::write refused
};

// next() returns the size of the next piece of compressed data and stores its
// address; 0 means there is no more (end of data or a read failure, which the
// caller tells apart itself). A piece must stay valid until the next call.
struct Input {
	void* context;
	size_t (*next)(void* context, const uint8_t** data);
};

struct Output {
	void* context;
	bool (*write)(void* context, const uint8_t* data, size_t size);
};

// Input over one memory block: Input{&memoryInput, MemoryInput::Next}.
struct MemoryInput {
	const uint8_t* data;
	size_t size;
	static size_t Next(void* context, const uint8_t** data)
	{
		MemoryInput& self = *static_cast<MemoryInput*>(context);
		const size_t size = self.size;
		*data = self.data;
		self.size = 0;
		return size;
	}
};

class Decoder {
public:
	// Decodes one raw DEFLATE stream. *produced receives the number of bytes
	// decoded, also on failure. Bytes after the final block are not consumed
	// beyond the piece they arrived in.
	Error Run(const Input& input, uint8_t* buffer, size_t capacity,
		const Output* output, uint64_t* produced = nullptr)
	{
		if (produced != nullptr)
			*produced = 0;
		if (input.next == nullptr || buffer == nullptr || capacity == 0
			|| (output != nullptr
				&& (output->write == nullptr || capacity < kWindowBytes)))
			return Error::BadArgument;
		in_ = input;
		buf_ = buffer;
		cap_ = capacity;
		out_ = output;
		pos_ = 0;
		total_ = 0;
		bits_ = 0;
		count_ = 0;
		cur_ = end_ = nullptr;
		eof_ = false;
		error_ = Error::None;
		for (;;) {
			uint32_t last, type;
			if (!Bits(1, last) || !Bits(2, type))
				break;
			if (type == 0) {
				if (!Stored())
					break;
			} else if (type == 1) {
				Fixed();
				if (!Block())
					break;
			} else if (type == 2) {
				if (!Dynamic() || !Block())
					break;
			} else {
				Fail(Error::Corrupt);
				break;
			}
			if (last)
				break;
		}
		if (error_ == Error::None && out_ != nullptr && pos_ != 0)
			Flush();
		if (produced != nullptr)
			*produced = total_;
		return error_;
	}

private:
	static constexpr int kFastBits = 9;
	struct Table {
		uint16_t count[16];
		uint16_t symbol[288];
		uint16_t fast[1 << kFastBits]; // length << 9 | symbol; 0 = long code
	};

	bool Fail(Error error)
	{
		if (error_ == Error::None)
			error_ = error;
		return false;
	}

	bool Pull()
	{
		if (eof_)
			return false;
		const uint8_t* data = nullptr;
		const size_t size = in_.next(in_.context, &data);
		if (size == 0 || data == nullptr) {
			eof_ = true;
			return false;
		}
		cur_ = data;
		end_ = data + size;
		return true;
	}

	bool Fill()
	{
		if (cur_ == end_ && !Pull())
			return false;
		bits_ |= uint64_t(*cur_++) << count_;
		count_ += 8;
		return true;
	}

	bool Bits(int n, uint32_t& value)
	{
		while (count_ < n) {
			if (!Fill())
				return Fail(Error::Truncated);
		}
		value = uint32_t(bits_) & ((1u << n) - 1);
		bits_ >>= n;
		count_ -= n;
		return true;
	}

	bool Flush()
	{
		if (out_ == nullptr)
			return Fail(Error::Overflow);
		if (!out_->write(out_->context, buf_, pos_))
			return Fail(Error::Sink);
		pos_ = 0;
		return true;
	}

	bool Put(uint8_t byte)
	{
		if (pos_ == cap_ && !Flush())
			return false;
		buf_[pos_++] = byte;
		total_++;
		return true;
	}

	static bool Build(Table& t, const uint8_t* lengths, int n)
	{
		for (int i = 0; i < 16; i++)
			t.count[i] = 0;
		for (int i = 0; i < n; i++)
			t.count[lengths[i]]++;
		t.count[0] = 0;
		int left = 1;
		for (int len = 1; len < 16; len++) {
			left <<= 1;
			left -= t.count[len];
			if (left < 0)
				return false; // over-subscribed
		}
		uint16_t offs[16], next[16];
		offs[1] = 0;
		for (int len = 1; len < 15; len++)
			offs[len + 1] = uint16_t(offs[len] + t.count[len]);
		uint32_t code = 0;
		next[0] = 0;
		for (int len = 1; len < 16; len++) {
			code = (code + t.count[len - 1]) << 1;
			next[len] = uint16_t(code);
		}
		for (int i = 0; i < (1 << kFastBits); i++)
			t.fast[i] = 0;
		for (int i = 0; i < n; i++) {
			const int len = lengths[i];
			if (len == 0)
				continue;
			t.symbol[offs[len]++] = uint16_t(i);
			const uint32_t canonical = next[len]++;
			if (len > kFastBits)
				continue;
			uint32_t reversed = 0;
			for (int b = 0; b < len; b++)
				reversed |= ((canonical >> b) & 1u) << (len - 1 - b);
			for (uint32_t k = reversed; k < (1u << kFastBits); k += 1u << len)
				t.fast[k] = uint16_t((len << 9) | i);
		}
		return true;
	}

	bool Decode(const Table& t, uint32_t& symbol)
	{
		while (count_ < 15 && Fill()) {
		}
		const uint32_t entry = t.fast[uint32_t(bits_) & ((1u << kFastBits) - 1)];
		if (entry != 0) {
			const int len = int(entry >> 9);
			if (len > count_)
				return Fail(Error::Truncated);
			bits_ >>= len;
			count_ -= len;
			symbol = entry & 511u;
			return true;
		}
		int code = 0, first = 0, index = 0;
		for (int len = 1; len < 16; len++) {
			if (len > count_)
				return Fail(eof_ ? Error::Truncated : Error::Corrupt);
			code |= int((bits_ >> (len - 1)) & 1u);
			const int count = t.count[len];
			if (code - count < first) {
				bits_ >>= len;
				count_ -= len;
				symbol = t.symbol[index + (code - first)];
				return true;
			}
			index += count;
			first += count;
			first <<= 1;
			code <<= 1;
		}
		return Fail(Error::Corrupt);
	}

	bool Stored()
	{
		const int drop = count_ & 7;
		bits_ >>= drop;
		count_ -= drop;
		uint32_t n, complement;
		if (!Bits(16, n) || !Bits(16, complement))
			return false;
		if ((n ^ 0xffffu) != complement)
			return Fail(Error::Corrupt);
		while (n != 0) {
			if (count_ != 0) {
				uint32_t byte;
				if (!Bits(8, byte) || !Put(uint8_t(byte)))
					return false;
				n--;
				continue;
			}
			if (cur_ == end_ && !Pull())
				return Fail(Error::Truncated);
			if (pos_ == cap_ && !Flush())
				return false;
			size_t take = size_t(end_ - cur_);
			if (take > n)
				take = n;
			if (take > cap_ - pos_)
				take = cap_ - pos_;
			memcpy(buf_ + pos_, cur_, take);
			cur_ += take;
			pos_ += take;
			total_ += take;
			n -= uint32_t(take);
		}
		return true;
	}

	void Fixed()
	{
		for (int i = 0; i < 144; i++)
			lengths_[i] = 8;
		for (int i = 144; i < 256; i++)
			lengths_[i] = 9;
		for (int i = 256; i < 280; i++)
			lengths_[i] = 7;
		for (int i = 280; i < 288; i++)
			lengths_[i] = 8;
		for (int i = 0; i < 30; i++)
			lengths_[288 + i] = 5;
		Build(lit_, lengths_, 288);
		Build(dist_, lengths_ + 288, 30);
	}

	bool Dynamic()
	{
		static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
			11, 4, 12, 3, 13, 2, 14, 1, 15};
		uint32_t hlit, hdist, hclen;
		if (!Bits(5, hlit) || !Bits(5, hdist) || !Bits(4, hclen))
			return false;
		const int nlit = int(hlit) + 257, ndist = int(hdist) + 1;
		if (nlit > 286 || ndist > 30)
			return Fail(Error::Corrupt);
		for (int i = 0; i < 19; i++)
			lengths_[i] = 0;
		for (int i = 0; i < int(hclen) + 4; i++) {
			uint32_t v;
			if (!Bits(3, v))
				return false;
			lengths_[order[i]] = uint8_t(v);
		}
		if (!Build(lit_, lengths_, 19))
			return Fail(Error::Corrupt);
		int index = 0;
		while (index < nlit + ndist) {
			uint32_t symbol;
			if (!Decode(lit_, symbol))
				return false;
			if (symbol < 16) {
				lengths_[320 + index++] = uint8_t(symbol);
				continue;
			}
			uint32_t repeat;
			uint8_t value = 0;
			if (symbol == 16) {
				if (index == 0)
					return Fail(Error::Corrupt);
				value = lengths_[320 + index - 1];
				if (!Bits(2, repeat))
					return false;
				repeat += 3;
			} else if (symbol == 17) {
				if (!Bits(3, repeat))
					return false;
				repeat += 3;
			} else {
				if (!Bits(7, repeat))
					return false;
				repeat += 11;
			}
			if (index + int(repeat) > nlit + ndist)
				return Fail(Error::Corrupt);
			while (repeat-- != 0)
				lengths_[320 + index++] = value;
		}
		if (lengths_[320 + 256] == 0)
			return Fail(Error::Corrupt); // no end-of-block code
		if (!Build(lit_, lengths_ + 320, nlit)
			|| !Build(dist_, lengths_ + 320 + nlit, ndist))
			return Fail(Error::Corrupt);
		return true;
	}

	bool Block()
	{
		static const uint16_t lengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11,
			13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
			163, 195, 227, 258};
		static const uint8_t lengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
			1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
		static const uint16_t distanceBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17,
			25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
			2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
		static const uint8_t distanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3,
			3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13,
			13};
		for (;;) {
			uint32_t symbol;
			if (!Decode(lit_, symbol))
				return false;
			if (symbol < 256) {
				if (!Put(uint8_t(symbol)))
					return false;
				continue;
			}
			if (symbol == 256)
				return true;
			symbol -= 257;
			if (symbol >= 29)
				return Fail(Error::Corrupt);
			uint32_t extra = 0, distanceSymbol;
			if (lengthExtra[symbol] != 0 && !Bits(lengthExtra[symbol], extra))
				return false;
			size_t length = lengthBase[symbol] + extra;
			if (!Decode(dist_, distanceSymbol))
				return false;
			if (distanceSymbol >= 30)
				return Fail(Error::Corrupt);
			extra = 0;
			if (distanceExtra[distanceSymbol] != 0
				&& !Bits(distanceExtra[distanceSymbol], extra))
				return false;
			const size_t distance = distanceBase[distanceSymbol] + extra;
			if (distance > total_)
				return Fail(Error::Corrupt);
			while (length-- != 0) {
				if (pos_ == cap_ && !Flush())
					return false;
				const size_t from = pos_ >= distance ? pos_ - distance
					: pos_ + cap_ - distance;
				buf_[pos_++] = buf_[from];
				total_++;
			}
		}
	}

	Input in_;
	const Output* out_;
	uint8_t* buf_;
	size_t cap_, pos_;
	uint64_t total_, bits_;
	int count_;
	const uint8_t* cur_;
	const uint8_t* end_;
	bool eof_;
	Error error_;
	Table lit_, dist_;
	uint8_t lengths_[320 + 320];
};

} // namespace hakefi::inflate
