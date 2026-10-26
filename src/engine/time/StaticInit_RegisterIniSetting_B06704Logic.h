#pragma once

// The sequence of 0x009DC2D0 (proposed StaticInit_RegisterIniSetting_B06704)
// over injected ports. Evidence: re/functions/0x009DC2D0.md and the export
// re/export/frame-clock/0x009DC2D0/asm.s. No engine binding exists yet: the
// game is not routed through RunStaticInit, only the tests drive it.
//
// The body has no branches and no handler code (read, 0x009DC2D0..0x009DC323).
// It links an MSVC EH frame with handler 0x009AF43A (read: push at 0x009DC2D2,
// FS:[0] store at 0x009DC2EA), sets the frame's try-state to 0 at 0x009DC2FA
// before the first call, never sets it back to -1, and unlinks the frame at
// 0x009DC318 (read). Its only exceptional behaviour is therefore an unwind of
// that frame, and whether the unwind happens is decided by the callers:
//   - a handler further out accepts the exception: the second (unwind) pass
//     calls 0x009AF43A, which runs the unwind action the frame's unwind map
//     assigns to state 0, before that handler's body runs;
//   - no handler further out accepts it: terminate (or the unhandled-exception
//     filter) runs while the frame is still in place, and state 0 is never
//     unwound. An outer SEH __except filter, if one exists, also runs in the
//     first (search) pass, before the unwind.
// Derived from the two-pass model of MSVC exception handling (Microsoft Learn,
// "Exceptions and stack unwinding in C++", step 4: no matching handler ->
// terminate; step 5: once the handler's parameter is initialised, "the process
// of unwinding the stack begins"). Unverified for this binary: 0x009AF43A and
// its tables are not in the export.
//
// What state 0's unwind action does - and whether it is a null action at all -
// is unverified: the unwind map behind 0x009AF43A is not in the export, only the
// try-state value is read (0x009DC2FA). re/functions/0x009DC2D0.md flow 2 marks
// it the same way. The port UnwindStateZero stands for that action, null or not.
//
// The rebuild expresses the frame as an object whose destructor calls that
// port while the stack unwinds, which is how MSVC's unwind-map entries work
// too. The test build's C++ ABI (Itanium, Linux clang -m32) is two-pass as
// well: a destructor runs only after the search pass found a handler. The
// standard leaves unwinding before terminate implementation-defined
// ([except.handle]); the test's flow 2c checks the "no handler, no unwind"
// behaviour on the toolchain that builds it.
//
// Not modelled: structured (SEH) exceptions. In the original, an access
// violation inside 0x004040E0 that a handler further out accepts would also
// make 0x009AF43A unwind state 0 (derived from the same two-pass model, where
// every frame handler is called in the unwind pass whatever the exception is;
// unverified, 0x009AF43A is not exported). The rebuild's destructor sees only
// C++ exceptions, and the Linux test build has no structured exceptions.
//
// Names are proposals until the frame-clock slice's refutation round.

#include <cstdint>

namespace obng::engine::time::static_init_b06704 {

// The plain values the body passes on.
struct Targets {
	std::uint32_t collection;  // ECX of 0x004040E0, loaded at 0x009DC2F5 (read)
	std::uint32_t setting;     // 0x004040E0's one stack argument, pushed at 0x009DC2F0 (read)
	std::uint32_t exitStub;    // _atexit's argument, pushed at 0x009DC307 (read)
};

// The EH frame from the link (0x009DC2EA) and the try-state store 0x009DC2FA
// to the unlink (0x009DC318). Nothing between the link and the store can
// raise a C++ exception (pushes and register moves, read), so one object
// covers both. Destroyed while linked - only during unwinding, because
// RunStaticInit unlinks it before every normal return - it runs state 0's
// unwind action.
template <class Ports>
class StateZeroFrame {
public:
	explicit StateZeroFrame(Ports& ports) : ports_(ports) {}
	StateZeroFrame(const StateZeroFrame&) = delete;
	StateZeroFrame& operator=(const StateZeroFrame&) = delete;

	~StateZeroFrame() {
		if (linked_) {
			ports_.UnwindStateZero();
		}
	}

	// 0x009DC318: FS:[0] gets the previous record back; the frame is gone
	// without its unwind action running (read: no call after 0x009DC30C).
	void Unlink() { linked_ = false; }

private:
	Ports& ports_;
	bool linked_ = true;
};

// The body's sequence over ports:
//   ports.AddSetting(collection, setting) - 0x004040E0, call at 0x009DC302
//   ports.RegisterAtExit(exitStub) -> int - _atexit 0x00981FB4, call at 0x009DC30C;
//                                           the result is not read (read)
//   ports.UnwindStateZero()               - state 0's unwind action in the map
//                                           behind 0x009AF43A (unverified what
//                                           it runs); reached only while a
//                                           handler further out unwinds the
//                                           stack (see above)
// No exception is caught here, as none is caught in the original.
template <class Ports>
void RunStaticInit(Ports& ports, const Targets& targets) {
	StateZeroFrame<Ports> frame(ports);
	ports.AddSetting(targets.collection, targets.setting);
	static_cast<void>(ports.RegisterAtExit(targets.exitStub));
	frame.Unlink();
}

} // namespace obng::engine::time::static_init_b06704
