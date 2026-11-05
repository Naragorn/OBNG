#pragma once

// The unrouted-draw probe's decisions ([Probe] UnroutedDraws,
// UnroutedDrawProbe.cpp), free of the engine and of Direct3D
// (plugin/tests/UnroutedDrawLogicTest.cpp).
//
// The stereo route doubles a device draw only inside its four detours. Any
// other draw of a single-pass frame reaches one eye. The probe sits on the
// device's four draw methods (IDirect3DDevice9 slots 81..84), sorts every
// call by where it falls, and keeps a row per calling site - the return
// address in the game's code - for the calls outside the route. The rows "in
// eye 0's viewport" are the draws the second eye misses; the rows "in
// another viewport" are the passes that do not draw into an eye's rectangle
// at all (shadow maps, the image-space effects, the interface).

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::unrouted {

// A viewport's rectangle (D3DVIEWPORT9's first four members).
struct Rect {
	std::uint32_t x = 0;
	std::uint32_t y = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

constexpr bool Same(const Rect& a, const Rect& b) {
	return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

enum class Method : std::uint8_t { kDrawPrimitive, kDrawIndexedPrimitive, kDrawPrimitiveUP, kDrawIndexedPrimitiveUP, kCount };

inline const char* MethodName(Method method) {
	switch (method) {
		case Method::kDrawPrimitive:
			return "DrawPrimitive";
		case Method::kDrawIndexedPrimitive:
			return "DrawIndexedPrimitive";
		case Method::kDrawPrimitiveUP:
			return "DrawPrimitiveUP";
		case Method::kDrawIndexedPrimitiveUP:
			return "DrawIndexedPrimitiveUP";
		case Method::kCount:
			break;
	}
	return "?";
}

enum class Where : std::uint8_t {
	kRouted,         // inside one of the route's four detours: both eyes get it
	kEyeZero,        // outside the route, eye data set, the viewport is eye 0's: the second eye misses it
	kOtherViewport,  // outside the route, eye data set, another viewport
	kNoEyes,         // no eye data: not a single-pass frame
	kCount,
};

inline const char* WhereName(Where where) {
	switch (where) {
		case Where::kRouted:
			return "routed";
		case Where::kEyeZero:
			return "in eye 0's viewport";
		case Where::kOtherViewport:
			return "in another viewport";
		case Where::kNoEyes:
			return "without eye data";
		case Where::kCount:
			break;
	}
	return "?";
}

constexpr Where Classify(bool insideRoute, bool eyesSet, const Rect& current, const Rect& eyeZero) {
	if (insideRoute) {
		return Where::kRouted;
	}
	if (!eyesSet) {
		return Where::kNoEyes;
	}
	return Same(current, eyeZero) ? Where::kEyeZero : Where::kOtherViewport;
}

constexpr std::uint32_t kSiteRows = 128;

struct SiteRow {
	std::uint32_t site = 0;  // the return address of the device call
	Method method = Method::kDrawPrimitive;
	Where where = Where::kEyeZero;
	Rect viewport;               // the first one seen
	bool moreViewports = false;  // a later call had another
	std::uint64_t calls = 0;
	std::uint64_t primitives = 0;
};

struct SiteTable {
	SiteRow rows[kSiteRows];
	std::uint32_t used = 0;
	std::uint64_t beyond = 0;  // calls of sites the table had no row for

	const SiteRow* Find(std::uint32_t site, Method method, Where where) const {
		for (std::uint32_t i = 0; i < used; ++i) {
			if (rows[i].site == site && rows[i].method == method && rows[i].where == where) {
				return &rows[i];
			}
		}
		return nullptr;
	}
	void Add(std::uint32_t site, Method method, Where where, const Rect& viewport, std::uint32_t primitives) {
		SiteRow* row = const_cast<SiteRow*>(Find(site, method, where));
		if (row == nullptr) {
			if (used == kSiteRows) {
				++beyond;
				return;
			}
			row = &rows[used++];
			row->site = site;
			row->method = method;
			row->where = where;
			row->viewport = viewport;
		} else if (!Same(row->viewport, viewport)) {
			row->moreViewports = true;
		}
		++row->calls;
		row->primitives += primitives;
	}
	// The row at `rank` by calls, the most first, ties in the table's order.
	const SiteRow* Ranked(std::uint32_t rank) const {
		const SiteRow* last = nullptr;
		for (std::uint32_t taken = 0; taken <= rank; ++taken) {
			const SiteRow* best = nullptr;
			for (std::uint32_t i = 0; i < used; ++i) {
				const SiteRow& row = rows[i];
				const bool afterLast = last == nullptr || row.calls < last->calls || (row.calls == last->calls && &row > last);
				if (afterLast && (best == nullptr || row.calls > best->calls)) {
					best = &row;
				}
			}
			if (best == nullptr) {
				return nullptr;
			}
			last = best;
		}
		return last;
	}
};

struct Stats {
	std::uint64_t calls[static_cast<int>(Where::kCount)] = {};
	SiteTable sites;

	void Add(Where where, Method method, std::uint32_t site, const Rect& viewport, std::uint32_t primitives) {
		if (static_cast<int>(where) >= static_cast<int>(Where::kCount) || static_cast<int>(method) >= static_cast<int>(Method::kCount)) {
			return;
		}
		++calls[static_cast<int>(where)];
		if (where != Where::kRouted) {
			sites.Add(site, method, where, viewport, primitives);
		}
	}
	void Reset() { *this = Stats{}; }
};

inline int FormatHead(const Stats& stats, std::uint64_t qpc, char* out, std::size_t capacity) {
	return std::snprintf(out, capacity,
	                     "Probe: device draws - %llu routed, %llu in eye 0's viewport outside the route, %llu in another viewport, %llu without "
	                     "eye data, %u sites, %llu calls beyond the table; qpc %llu",
	                     static_cast<unsigned long long>(stats.calls[static_cast<int>(Where::kRouted)]),
	                     static_cast<unsigned long long>(stats.calls[static_cast<int>(Where::kEyeZero)]),
	                     static_cast<unsigned long long>(stats.calls[static_cast<int>(Where::kOtherViewport)]),
	                     static_cast<unsigned long long>(stats.calls[static_cast<int>(Where::kNoEyes)]), stats.sites.used,
	                     static_cast<unsigned long long>(stats.sites.beyond), static_cast<unsigned long long>(qpc));
}

// The site at `rank` by calls, or nothing (0) past the rows.
inline int FormatSite(const Stats& stats, std::uint32_t rank, char* out, std::size_t capacity) {
	const SiteRow* row = stats.sites.Ranked(rank);
	if (row == nullptr) {
		return 0;
	}
	return std::snprintf(out, capacity, "Probe: device draw site 0x%08X %s %s - %llu calls, %llu primitives, viewport %u,%u %ux%u%s", row->site,
	                     MethodName(row->method), WhereName(row->where), static_cast<unsigned long long>(row->calls),
	                     static_cast<unsigned long long>(row->primitives), row->viewport.x, row->viewport.y, row->viewport.width,
	                     row->viewport.height, row->moreViewports ? " and others" : "");
}

}  // namespace obng::plugin::unrouted
