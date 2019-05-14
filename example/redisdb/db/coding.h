#pragma once

#include <stdint.h>
#include <string.h>

#include <string>
#include <string_view>

static const bool kLittleEndian = true /* or some other expression */;

// Standard Put... routines append to a string
void PutFixed32(std::string* dst, uint32_t value);

void PutFixed64(std::string* dst, uint64_t value);

void PutVarint32(std::string* dst, uint32_t value);

void PutVarint64(std::string* dst, uint64_t value);

void PutLengthPrefixedSlice(std::string* dst, const std::string_view& value);

// Standard Get... routines parse a value from the beginning of a Slice
// and advance the slice past the parsed value.
bool GetVarint32(std::string_view* input, uint32_t* value);

bool GetVarint64(std::string_view* input, uint64_t* value);

bool GetLengthPrefixedSlice(std::string_view* input, std::string_view* result);

std::string_view GetLengthPrefixedSlice(const char* data);

// Pointer-based variants of GetVarint...  These either store a value
// in *v and return a pointer just past the parsed value, or return
// nullptr on error.  These routines only look at bytes in the range
// [p..limit-1]
const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* v);

const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* v);

// Returns the length of the varint32 or varint64 encoding of "v"
int VarintLength(uint64_t v);

// Lower-level versions of Put... that Write directly into a character buffer
// REQUIRES: dst has enough space for the value being written
void EncodeFixed32(char* dst, uint32_t value);

void EncodeFixed64(char* dst, uint64_t value);

// Lower-level versions of Put... that Write directly into a character buffer
// and return a pointer just past the last byte written.
// REQUIRES: dst has enough space for the value being written
char* EncodeVarint32(char* dst, uint32_t value);

char* EncodeVarint64(char* dst, uint64_t value);

// Lower-level versions of Get... that read directly from a character buffer
// without any bounds checking.

inline uint32_t DecodeFixed32(const char* ptr) {
	if (!kLittleEndian) {
		uint32_t result;
		memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
		return result;
	}
	else {
		return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0])))
			| (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1]))<< 8)
			| (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2]))<< 16)
			| (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3]))<< 24));
	}
}

inline uint64_t DecodeFixed64(const char* ptr) {
	if (!kLittleEndian) {
		uint64_t result;
		memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
		return result;
	}
	else {
		uint64_t lo = DecodeFixed32(ptr);
		uint64_t hi = DecodeFixed32(ptr + 4);
		return (hi<< 32) | lo;
	}
}

// Internal routine for use by fallback path of GetVarint32Ptr
const char* GetVarint32PtrFallback(const char* p, const char* limit, uint32_t * value);

inline const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t * value) {
	if (p< limit) {
		uint32_t result = *(reinterpret_cast<const unsigned char*>(p));
		if ((result & 128) == 0) {
			*value = result;
			return p + 1;
		}
	}
	return GetVarint32PtrFallback(p, limit, value);
}

