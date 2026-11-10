// Every flow of keeping a finished session's log (plugin/src/LogKeepLogic.h).

#include "Check.h"
#include "LogKeepLogic.h"

using namespace obng::plugin::logkeep;

int main() {
	Section("KeptName: the log's last write as a name that sorts by time");
	Check(KeptName(Stamp{2026, 11, 10, 22, 20, 5}) == "OBNG-2026-11-10_22-20-05.log", "padded fields");
	Check(KeptName(Stamp{2026, 1, 2, 3, 4, 5}) < KeptName(Stamp{2026, 1, 2, 3, 4, 6}), "a later second sorts later");
	Check(KeptName(Stamp{2026, 9, 30, 23, 59, 59}) < KeptName(Stamp{2026, 10, 1, 0, 0, 0}), "a later month sorts later");
	Check(IsKeptName(KeptName(Stamp{123456, 111, 222, 333, 444, 555})), "fields out of range are cut to the shape");

	Section("IsKeptName: only what KeptName writes");
	Check(IsKeptName("OBNG-2026-11-10_22-20-05.log"), "the shape");
	Check(!IsKeptName("OBNG.log") && !IsKeptName("") && !IsKeptName("notes.txt"), "other files");
	Check(!IsKeptName("OBNG-2026-11-10_22-20-05.log.bak") && !IsKeptName("OBNG-2026-11-10_22-20-0.log"), "another length");
	Check(!IsKeptName("OBNG-2026-11-1x_22-20-05.log"), "a letter where a digit belongs");
	Check(!IsKeptName("OBVR-2026-11-10_22-20-05.log") && !IsKeptName("OBNG-2026-11-10 22-20-05.log"), "another fixed character");

	Section("ToRemove: the oldest kept logs beyond the count, nothing else");
	const std::vector<std::string> names = {"OBNG-2026-11-10_22-20-05.log", "notes.txt", "OBNG-2026-11-08_10-00-00.log", "OBNG-2026-11-09_10-00-00.log",
	                                        "OBNG-2026-11-07_10-00-00.log"};
	Check(ToRemove(names, 4).empty() && ToRemove(names, 9).empty(), "at or under the count: nothing");
	Check(ToRemove(names, 2) == std::vector<std::string>({"OBNG-2026-11-07_10-00-00.log", "OBNG-2026-11-08_10-00-00.log"}), "the two oldest of four");
	Check(ToRemove(names, 0).size() == 4, "count 0: every kept log, never the other file");
	Check(ToRemove({}, 3).empty() && ToRemove({"notes.txt"}, 0).empty(), "nothing kept: nothing");
	return Failures();
}
