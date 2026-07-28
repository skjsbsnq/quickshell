#include "write_all.hpp"

#include <algorithm>
#include <cerrno>

#include <unistd.h>

namespace qs::crash {

bool writeAllChunked(int fd, const void* data, size_t len, size_t maxChunk) noexcept {
	// NOLINTBEGIN (cppcoreguidelines-pro-bounds-pointer-arithmetic)
	auto* wptr = static_cast<const char*>(data);
	auto* end = wptr + len;
	while (wptr != end) {
		auto remaining = static_cast<size_t>(end - wptr);
		auto chunk = maxChunk == 0 ? remaining : std::min(remaining, maxChunk);
		auto r = ::write(fd, wptr, chunk);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) return false;
		wptr += static_cast<size_t>(r);
	}
	// NOLINTEND
	return true;
}

bool writeAll(int fd, const void* data, size_t len) noexcept {
	return writeAllChunked(fd, data, len, 0);
}

} // namespace qs::crash
