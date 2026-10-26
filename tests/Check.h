#pragma once

// The tests' whole framework: a check prints ok or FAIL with what it checked,
// a section names the flow under test, and main returns the failure count.
// OBVR's tests have the same shape; no library, nothing to install.
//
//   int main() {
//       Section("A zero frame reads as one millisecond");
//       Check(FrameMs(0) == 1, "zero clamped to the floor");
//       return Failures();
//   }

#include <cstdio>

namespace obng::test {

inline int& FailureCount() {
	static int failures = 0;
	return failures;
}

} // namespace obng::test

inline void Section(const char* what) {
	std::printf("%s\n", what);
}

inline void Check(bool condition, const char* what) {
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition) {
		++obng::test::FailureCount();
	}
}

inline int Failures() {
	int failures = obng::test::FailureCount();
	std::printf("%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
	return failures;
}
