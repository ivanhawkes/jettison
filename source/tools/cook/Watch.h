#pragma once

#include <atomic>
#include <filesystem>

namespace Jettison::Cook
{
// The artists' daemon (docs/dense-geometry-and-ray-tracing.md, "The cook"): the source tree cooked
// into the output tree by CookTree, then cooked again each time a change to it settles (is followed by
// 250 ms with no other, or at most 2 s after the first, so changes that never stop cannot hold a cook
// back), until stop is set, which it sees within a fraction of a second. A change is a file written,
// made, deleted or renamed anywhere in the source but under a hidden directory or file (a leading dot),
// which are not sources; the record decides what it cooks again, so a changed buffer, image or settings
// file cooks every source that uses it. A cook that refuses a source, or throws, is logged and the
// watch goes on, a refused source's outputs kept. Throws std::runtime_error, before watching or
// cooking, for trees CheckTrees refuses or a source that cannot be watched.
void WatchTree(
    const std::filesystem::path &source, const std::filesystem::path &output,
    const std::atomic<bool> &stop);
} // namespace Jettison::Cook
