#pragma once

// The log of a finished session kept instead of overwritten: where it goes
// and which kept logs go when there are too many - over plain values.
//
// At its start the plugin moves an existing OBNG.log to
// OBNG-logs\OBNG-YYYY-MM-DD_HH-MM-SS.log, named by the log's last write (the
// end of the session it belongs to, the shape of OBVR's OBVR-logs), and then
// drops the oldest kept logs beyond a count. The names sort by time as text.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace obng::plugin::logkeep {

constexpr const char* kFolder = "OBNG-logs";
constexpr std::size_t kKeep = 40;

struct Stamp {
	unsigned year = 0;
	unsigned month = 0;
	unsigned day = 0;
	unsigned hour = 0;
	unsigned minute = 0;
	unsigned second = 0;
};

// "OBNG-2026-11-10_22-20-05.log"
inline std::string KeptName(const Stamp& s) {
	char name[48];
	std::snprintf(name, sizeof(name), "OBNG-%04u-%02u-%02u_%02u-%02u-%02u.log", s.year % 10000u, s.month % 100u, s.day % 100u, s.hour % 100u,
	              s.minute % 100u, s.second % 100u);
	return name;
}

// A file of the folder that this keeping made (and may therefore remove):
// exactly the shape KeptName writes. Anything else in the folder stays.
inline bool IsKeptName(const std::string& name) {
	static const char shape[] = "OBNG-dddd-dd-dd_dd-dd-dd.log";
	if (name.size() != sizeof(shape) - 1) {
		return false;
	}
	for (std::size_t i = 0; i < name.size(); ++i) {
		if (shape[i] == 'd' ? (name[i] < '0' || name[i] > '9') : name[i] != shape[i]) {
			return false;
		}
	}
	return true;
}

// The kept logs to remove so that at most `keep` stay: the oldest ones.
inline std::vector<std::string> ToRemove(const std::vector<std::string>& names, std::size_t keep) {
	std::vector<std::string> kept;
	for (const std::string& n : names) {
		if (IsKeptName(n)) {
			kept.push_back(n);
		}
	}
	std::sort(kept.begin(), kept.end());
	if (kept.size() <= keep) {
		return {};
	}
	kept.resize(kept.size() - keep);
	return kept;
}

}  // namespace obng::plugin::logkeep
