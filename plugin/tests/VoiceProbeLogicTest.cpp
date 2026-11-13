// Every flow of the voice lines' probe's logic (plugin/src/VoiceProbeLogic.h).

#include "Check.h"
#include "VoiceProbeLogic.h"

#include <cstring>
#include <string>

using namespace obng::plugin::voice;

int main() {
	Section("The path as the log takes it");
	{
		char out[64];
		const char* plain = "Data\\Sound\\Voice\\Oblivion.esm\\Imperial\\M\\Hello_0001.mp3";
		Check(CleanPath(plain, out, sizeof(out)) == std::strlen(plain) && std::strcmp(out, plain) == 0, "a plain path: as it is");
		Check(CleanPath(nullptr, out, sizeof(out)) == 6 && std::strcmp(out, "(none)") == 0, "no path: said so");
		Check(CleanPath("a\tb\x80" "c", out, sizeof(out)) == 5 && std::strcmp(out, "a?b?c") == 0, "what is not printable ASCII becomes '?'");
		char small[5];
		Check(CleanPath("abcdefgh", small, sizeof(small)) == 4 && std::strcmp(small, "abcd") == 0, "a small buffer: cut, terminated");
		const std::string longPath(400, 'x');
		char big[512];
		Check(CleanPath(longPath.c_str(), big, sizeof(big)) == kPathAtMost && std::strlen(big) == kPathAtMost, "a long path: cut at the most the log takes");
		char none[1] = {'z'};
		Check(CleanPath("abc", none, 0) == 0 && none[0] == 'z', "no room at all: nothing written");
		Check(CleanPath("", out, sizeof(out)) == 0 && out[0] == 0, "an empty path: empty");
	}

	Section("The line");
	{
		char line[128];
		FormatLine("a\\b.mp3", 123456, 10000000, line, sizeof(line));
		Check(std::strcmp(line, "Probe: voice line 12.346 ms - a\\b.mp3") == 0, "the time in milliseconds, then the path");
		FormatLine("x", 5, 0, line, sizeof(line));
		Check(std::strcmp(line, "Probe: voice line 0.000 ms - x") == 0, "no clock frequency: 0 ms");
	}

	Section("Which calls are written");
	{
		Check(Writes(100, 100, 0, 10), "at the threshold: written");
		Check(!Writes(99, 100, 0, 10), "under it: not");
		Check(!Writes(1000, 100, 10, 10), "the window's lines used up: not");
		Check(Writes(0, 0, 0, 1), "a threshold of 0 writes every call");
	}

	return Failures();
}
