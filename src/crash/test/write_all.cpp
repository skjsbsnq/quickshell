#include "write_all.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <qtest.h>
#include <qtestcase.h>
#include <unistd.h>

#include "../write_all.hpp"

using qs::crash::writeAll;
using qs::crash::writeAllChunked;

namespace {

// sizeof(cpptrace::safe_object_frame) is 4120 on this platform; use the same
// magnitude so the test exercises realistic crash-handler payload sizes.
constexpr size_t kFrameSize = 4120;

std::vector<uint8_t> makePattern(size_t len, uint8_t seed) {
	std::vector<uint8_t> buf(len);
	for (size_t i = 0; i < len; i++) {
		buf[i] = static_cast<uint8_t>(seed + static_cast<uint8_t>(i) + static_cast<uint8_t>(i >> 8));
	}
	return buf;
}

// Reader that drains an fd into a vector until EOF (write end closed).
std::vector<uint8_t> drainFd(int fd) {
	std::vector<uint8_t> out;
	std::array<uint8_t, 1024> tmp {};
	while (true) {
		auto n = ::read(fd, tmp.data(), tmp.size());
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) break;
		out.insert(out.end(), tmp.begin(), tmp.begin() + n);
	}
	return out;
}

struct Pipe {
	int r = -1;
	int w = -1;

	Pipe() {
		int fds[2] = {-1, -1};
		if (::pipe(fds) != 0) {
			r = w = -1;
			return;
		}
		r = fds[0];
		w = fds[1];
	}

	~Pipe() {
		if (r != -1) ::close(r);
		if (w != -1) ::close(w);
	}

	Pipe(const Pipe&) = delete;
	Pipe& operator=(const Pipe&) = delete;
};

} // namespace

void TestWriteAll::fullWriteUncapped() {
	Pipe p;
	QVERIFY(p.r != -1);

	auto payload = makePattern(kFrameSize, 0x11);
	std::vector<uint8_t> received;
	std::thread reader([&] { received = drainFd(p.r); });

	QVERIFY(writeAll(p.w, payload.data(), payload.size()));
	::close(p.w);
	p.w = -1;
	reader.join();

	QCOMPARE(received, payload);
}

void TestWriteAll::partialChunksNoDuplication() {
	// Force every underlying write to at most 7 bytes — many partials.
	Pipe p;
	QVERIFY(p.r != -1);

	auto payload = makePattern(kFrameSize, 0x22);
	std::vector<uint8_t> received;
	std::thread reader([&] { received = drainFd(p.r); });

	QVERIFY(writeAllChunked(p.w, payload.data(), payload.size(), 7));
	::close(p.w);
	p.w = -1;
	reader.join();

	QCOMPARE(received.size(), payload.size());
	QCOMPARE(received, payload);

	// Sanity: the buggy "always write &frame, sizeof(frame)" loop would
	// produce a stream whose first 7 bytes repeat as the prefix of every
	// chunk. Assert that does NOT happen for a mid-stream window.
	if (payload.size() > 14) {
		// Bytes [7,14) must equal payload[7,14), not payload[0,7).
		QVERIFY(std::memcmp(received.data() + 7, payload.data() + 7, 7) == 0);
		QVERIFY(std::memcmp(received.data() + 7, payload.data(), 7) != 0);
	}
}

void TestWriteAll::partialChunksVaryingSizes() {
	Pipe p;
	QVERIFY(p.r != -1);

	auto payload = makePattern(2048, 0x33);
	std::vector<uint8_t> received;
	std::thread reader([&] { received = drainFd(p.r); });

	// 1-byte chunks: worst-case partial write density.
	QVERIFY(writeAllChunked(p.w, payload.data(), payload.size(), 1));
	::close(p.w);
	p.w = -1;
	reader.join();

	QCOMPARE(received, payload);
}

void TestWriteAll::multiFrameSequence() {
	// Crash handler writes many frames back-to-back; verify no cross-frame
	// duplication when each frame itself is fragmented.
	Pipe p;
	QVERIFY(p.r != -1);

	constexpr size_t frameCount = 3;
	std::vector<uint8_t> expected;
	expected.reserve(frameCount * kFrameSize);
	for (size_t i = 0; i < frameCount; i++) {
		auto frame = makePattern(kFrameSize, static_cast<uint8_t>(0x40 + i));
		expected.insert(expected.end(), frame.begin(), frame.end());
	}

	std::vector<uint8_t> received;
	std::thread reader([&] { received = drainFd(p.r); });

	for (size_t i = 0; i < frameCount; i++) {
		auto frame = makePattern(kFrameSize, static_cast<uint8_t>(0x40 + i));
		QVERIFY(writeAllChunked(p.w, frame.data(), frame.size(), 64));
	}
	::close(p.w);
	p.w = -1;
	reader.join();

	QCOMPARE(received.size(), expected.size());
	QCOMPARE(received, expected);
}

void TestWriteAll::hardErrorReturnsFalse() {
	// Writing to a closed / invalid fd must fail cleanly (no infinite loop).
	QVERIFY(!writeAll(-1, "x", 1));
	QVERIFY(!writeAllChunked(-1, "x", 1, 1));
}

void TestWriteAll::smallPipeBufferNoDuplication() {
	// Acceptance: inject a small pipe buffer so the kernel itself returns
	// partial writes (sizeof(safe_object_frame) == 4120 > min pipe size 4096).
	Pipe p;
	QVERIFY(p.r != -1);

#ifdef F_SETPIPE_SZ
	auto setSize = ::fcntl(p.w, F_SETPIPE_SZ, 4096);
	QVERIFY2(setSize >= 4096, "F_SETPIPE_SZ(4096) failed");
#endif

	// Two frames → 8240 bytes, well above a 4096 pipe, so the writer blocks
	// and resumes after the reader drains — classic partial-write scenario.
	constexpr size_t frameCount = 2;
	std::vector<uint8_t> expected;
	for (size_t i = 0; i < frameCount; i++) {
		auto frame = makePattern(kFrameSize, static_cast<uint8_t>(0x50 + i));
		expected.insert(expected.end(), frame.begin(), frame.end());
	}

	std::vector<uint8_t> received;
	std::thread reader([&] {
		// Slow drain to keep the pipe under pressure longer.
		std::vector<uint8_t> out;
		std::array<uint8_t, 256> tmp {};
		while (true) {
			auto n = ::read(p.r, tmp.data(), tmp.size());
			if (n < 0 && errno == EINTR) continue;
			if (n <= 0) break;
			out.insert(out.end(), tmp.begin(), tmp.begin() + n);
		}
		received = std::move(out);
	});

	for (size_t i = 0; i < frameCount; i++) {
		auto frame = makePattern(kFrameSize, static_cast<uint8_t>(0x50 + i));
		QVERIFY(writeAll(p.w, frame.data(), frame.size()));
	}
	::close(p.w);
	p.w = -1;
	reader.join();

	QCOMPARE(received.size(), expected.size());
	QCOMPARE(received, expected);
}

QTEST_MAIN(TestWriteAll);
