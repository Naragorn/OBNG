// Checks the test framework itself and that the toolchain is the 32-bit one
// the layout structs need: a passing check leaves the count alone, a failing
// one raises it, and a pointer is four bytes wide.

#include "Check.h"

int main() {
	Section("The 32-bit toolchain");
	Check(sizeof(void*) == 4, "a pointer is four bytes, as in Oblivion.exe");

	Section("The check counts failures");
	Check(true, "a passing check");
	int before = obng::test::FailureCount();
	Check(false, "a failing check, expected here");
	bool counted = obng::test::FailureCount() == before + 1;
	obng::test::FailureCount() = before;
	Check(counted, "the failing check raised the count by one");

	return Failures();
}
