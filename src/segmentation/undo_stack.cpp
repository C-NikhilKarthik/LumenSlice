#include "segmentation/undo_stack.hpp"

#include <cstddef>
#include <cstring>

namespace lumen {

// RLE record: one value byte followed by a 4-byte little-endian run length. Runs
// longer than 2^32-1 are split across records (never happens for real volumes, but
// keeps the format total).
std::vector<std::uint8_t> UndoStack::encode(const LabelVolume& mask) {
    std::vector<std::uint8_t> out;
    const std::uint8_t* data = mask.data();
    const std::size_t n = mask.voxel_count();
    out.reserve(64);
    std::size_t i = 0;
    while (i < n) {
        const std::uint8_t v = data[i];
        std::size_t run = 1;
        while (i + run < n && data[i + run] == v &&
               run < 0xFFFFFFFFull) {
            ++run;
        }
        const std::uint32_t r = static_cast<std::uint32_t>(run);
        out.push_back(v);
        out.push_back(static_cast<std::uint8_t>(r & 0xFF));
        out.push_back(static_cast<std::uint8_t>((r >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((r >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((r >> 24) & 0xFF));
        i += run;
    }
    return out;
}

void UndoStack::decode(const std::vector<std::uint8_t>& rle, LabelVolume& mask) {
    std::uint8_t* data = mask.data();
    const std::size_t n = mask.voxel_count();
    std::size_t out = 0;
    std::size_t p = 0;
    while (p + 4 < rle.size() && out < n) {
        const std::uint8_t v = rle[p];
        const std::uint32_t run = static_cast<std::uint32_t>(rle[p + 1]) |
                                  (static_cast<std::uint32_t>(rle[p + 2]) << 8) |
                                  (static_cast<std::uint32_t>(rle[p + 3]) << 16) |
                                  (static_cast<std::uint32_t>(rle[p + 4]) << 24);
        p += 5;
        std::size_t count = run;
        if (out + count > n) count = n - out;
        std::memset(data + out, v, count);
        out += count;
    }
}

void UndoStack::capture(const LabelVolume& mask) {
    if (!mask.valid()) return;
    past_.push_back(encode(mask));
    if (past_.size() > kDepth) past_.pop_front();
    future_.clear();
}

bool UndoStack::undo(LabelVolume& mask) {
    if (past_.empty() || !mask.valid()) return false;
    future_.push_back(encode(mask));
    if (future_.size() > kDepth) future_.pop_front();
    decode(past_.back(), mask);
    past_.pop_back();
    return true;
}

bool UndoStack::redo(LabelVolume& mask) {
    if (future_.empty() || !mask.valid()) return false;
    past_.push_back(encode(mask));
    if (past_.size() > kDepth) past_.pop_front();
    decode(future_.back(), mask);
    future_.pop_back();
    return true;
}

void UndoStack::reset() {
    past_.clear();
    future_.clear();
}

} // namespace lumen
