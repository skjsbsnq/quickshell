#pragma once

#include <cstddef>

namespace qs::crash {

// Async-signal-safe full write: retries EINTR and resumes from the remaining
// unwritten tail after partial writes (F-04 / T-06). Returns false on hard error
// or end-of-file before all bytes are written.
[[nodiscard]] bool writeAll(int fd, const void* data, size_t len) noexcept;

// Test seam: same loop as writeAll, but each underlying write is capped at
// maxChunk bytes so partial-write behavior can be exercised without depending
// on kernel pipe buffer sizes. maxChunk == 0 means uncapped (same as writeAll).
[[nodiscard]] bool writeAllChunked(int fd, const void* data, size_t len, size_t maxChunk) noexcept;

} // namespace qs::crash
