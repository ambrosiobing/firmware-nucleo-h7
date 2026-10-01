// test/cpp_encode_only.cpp: a translation unit that contains the C++ codec and
// nothing else, so that its object size is comparable with payload.c's.
//
// Compiling the filter would measure printf and sscanf, not the codec. This
// file exists only to make the size row in the README an honest comparison: one
// object holding one encoder and one decoder, built at -Os, against the C one
// built the same way.
#include "payload.hpp"

extern "C" {

void payload_cpp_encode(const payload::Values *in, std::uint8_t *out)
{
    const payload::Buffer buf = payload::encode(*in);
    for (std::size_t i = 0; i < payload::total_bytes; ++i) {
        out[i] = buf[i];
    }
}

void payload_cpp_decode(const std::uint8_t *in, payload::Values *out)
{
    payload::Buffer buf{};
    for (std::size_t i = 0; i < payload::total_bytes; ++i) {
        buf[i] = in[i];
    }
    *out = payload::decode(buf);
}

}  // extern "C"
