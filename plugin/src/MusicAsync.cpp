#include "MusicAsync.h"

#include <windows.h>
#include <ole2.h>
#include <dshow.h>

#include <atomic>
#include <initializer_list>
#include <cstring>

#include "Memory.h"
#include "engine/sound/MusicAsyncLogic.h"

namespace obng::plugin {

namespace {

namespace ma = obng::engine::sound::music_async;

// The music player's seven calls of CoCreateInstance(CLSID_FilterGraph, null, CLSCTX_INPROC_SERVER, IID_IGraphBuilder,
// this+0x70), `call [0x00A283C8]` (read 2026-11-19; src/engine/sound/MusicAsyncLogic.h).
constexpr UInt32 kSites[] = {0x006AB366, 0x006ACF46, 0x006AD216, 0x006AD4F8, 0x006AD806, 0x006ADB76, 0x006ADD56};
constexpr UInt8 kSiteBytes[] = {0xFF, 0x15, 0xC8, 0x83, 0xA2, 0x00};
constexpr UInt32 kCoCreateInstanceSlot = 0x00A283C8;
constexpr CLSID kFilterGraph = {0xE436EBB3, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};  // the game's 0x00A78FB4
constexpr UInt32 kTrackLines = 64;
constexpr UInt32 kQueue = 16;

using CreateFn = HRESULT(__stdcall*)(const CLSID*, IUnknown*, DWORD, const IID*, void**);
using InitializeExFn = HRESULT(__stdcall*)(void*, DWORD);

LogFn g_log = nullptr;
bool g_on = false;
CreateFn g_create = nullptr;
std::atomic<unsigned> g_tracks{0}, g_failed{0}, g_waits{0}, g_lines{0};
double g_ticksPerMs = 0.0;

double Ms(LONGLONG ticks) {
	return static_cast<double>(ticks) / g_ticksPerMs;
}

LONGLONG Now() {
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

class MusicGraph;
void Enqueue(MusicGraph* graph);

// The stand-in graph the game gets: what it asks before the real graph is built is kept (MusicAsyncLogic.h) and given
// to the real one at the first call on the main thread after the worker is done; afterwards every call goes through.
// The methods the game never calls (read: the music player's code) wait for the worker and pass on.
class MusicGraph final : public IGraphBuilder, public IBasicAudio, public IMediaControl, public IMediaPosition, public IMediaEvent {
public:
	// The worker's part: the real graph made and rendered on its thread (COM's multithreaded apartment).
	void Build() {
		const LONGLONG t0 = Now();
		IGraphBuilder* real = nullptr;
		HRESULT result = g_create(&kFilterGraph, nullptr, CLSCTX_INPROC_SERVER, &__uuidof(IGraphBuilder), reinterpret_cast<void**>(&real));
		if (SUCCEEDED(result)) {
			result = real->RenderFile(path_, nullptr);
			if (FAILED(result)) {
				real->Release();
				real = nullptr;
			}
		}
		buildTicks_ = Now() - t0;
		buildResult_ = result;
		real_ = real;
		phase_.store(real != nullptr ? ma::Phase::Ready : ma::Phase::Failed, std::memory_order_release);
		Release();  // the worker's reference
	}

	// IUnknown, for all five.
	STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
		if (out == nullptr) {
			return E_POINTER;
		}
		Sync();
		*out = Self(iid);
		if (*out != nullptr) {
			AddRef();
			return S_OK;
		}
		IGraphBuilder* real = Real();
		return real != nullptr ? real->QueryInterface(iid, out) : E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(refs_.fetch_add(1) + 1); }
	STDMETHODIMP_(ULONG) Release() override {
		const long left = refs_.fetch_sub(1) - 1;
		if (left == 0) {
			delete this;
		}
		return static_cast<ULONG>(left);
	}

	// IDispatch, for the four that have it: the game never asks (read); not served.
	STDMETHODIMP GetTypeInfoCount(UINT*) override { return E_NOTIMPL; }
	STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
	STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
	STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }

	// IFilterGraph and IGraphBuilder: RenderFile starts the worker; the rest is never called by the game.
	STDMETHODIMP RenderFile(LPCWSTR file, LPCWSTR playList) override {
		if (queued_) {
			IGraphBuilder* real = Real();
			return real != nullptr ? real->RenderFile(file, playList) : E_FAIL;
		}
		const LONGLONG t0 = Now();
		lstrcpynW(path_, file != nullptr ? file : L"", MAX_PATH);
		queued_ = true;
		askedTick_ = t0;
		AddRef();  // the worker's reference, given back at the end of Build
		Enqueue(this);
		g_tracks.fetch_add(1, std::memory_order_relaxed);
		return S_OK;
	}
	STDMETHODIMP AddFilter(IBaseFilter* filter, LPCWSTR name) override { return Pass([&](IGraphBuilder* r) { return r->AddFilter(filter, name); }); }
	STDMETHODIMP RemoveFilter(IBaseFilter* filter) override { return Pass([&](IGraphBuilder* r) { return r->RemoveFilter(filter); }); }
	STDMETHODIMP EnumFilters(IEnumFilters** out) override { return Pass([&](IGraphBuilder* r) { return r->EnumFilters(out); }); }
	STDMETHODIMP FindFilterByName(LPCWSTR name, IBaseFilter** out) override {
		return Pass([&](IGraphBuilder* r) { return r->FindFilterByName(name, out); });
	}
	STDMETHODIMP ConnectDirect(IPin* out, IPin* in, const AM_MEDIA_TYPE* type) override {
		return Pass([&](IGraphBuilder* r) { return r->ConnectDirect(out, in, type); });
	}
	STDMETHODIMP Reconnect(IPin* pin) override { return Pass([&](IGraphBuilder* r) { return r->Reconnect(pin); }); }
	STDMETHODIMP Disconnect(IPin* pin) override { return Pass([&](IGraphBuilder* r) { return r->Disconnect(pin); }); }
	STDMETHODIMP SetDefaultSyncSource() override { return Pass([&](IGraphBuilder* r) { return r->SetDefaultSyncSource(); }); }
	STDMETHODIMP Connect(IPin* out, IPin* in) override { return Pass([&](IGraphBuilder* r) { return r->Connect(out, in); }); }
	STDMETHODIMP Render(IPin* out) override { return Pass([&](IGraphBuilder* r) { return r->Render(out); }); }
	STDMETHODIMP AddSourceFilter(LPCWSTR file, LPCWSTR name, IBaseFilter** out) override {
		return Pass([&](IGraphBuilder* r) { return r->AddSourceFilter(file, name, out); });
	}
	STDMETHODIMP SetLogFile(DWORD_PTR file) override { return Pass([&](IGraphBuilder* r) { return r->SetLogFile(file); }); }
	STDMETHODIMP Abort() override { return Pass([&](IGraphBuilder* r) { return r->Abort(); }); }
	STDMETHODIMP ShouldOperationContinue() override { return Pass([&](IGraphBuilder* r) { return r->ShouldOperationContinue(); }); }

	// IBasicAudio: the volume is kept until the real graph is there.
	STDMETHODIMP put_Volume(long volume) override {
		Sync();
		if (audio_ != nullptr) {
			return audio_->put_Volume(volume);
		}
		ma::AskVolume(intent_, volume);
		return S_OK;
	}
	STDMETHODIMP get_Volume(long* volume) override {
		Sync();
		if (audio_ != nullptr) {
			return audio_->get_Volume(volume);
		}
		if (volume == nullptr) {
			return E_POINTER;
		}
		*volume = intent_.volume;
		return S_OK;
	}
	STDMETHODIMP put_Balance(long balance) override { return PassAudio([&](IBasicAudio* a) { return a->put_Balance(balance); }); }
	STDMETHODIMP get_Balance(long* balance) override { return PassAudio([&](IBasicAudio* a) { return a->get_Balance(balance); }); }

	// IMediaControl: the run state is kept until the real graph is there.
	STDMETHODIMP Run() override { return AskOrPass(ma::Run::Running); }
	STDMETHODIMP Pause() override { return AskOrPass(ma::Run::Paused); }
	STDMETHODIMP Stop() override { return AskOrPass(ma::Run::Stopped); }
	STDMETHODIMP GetState(LONG timeout, OAFilterState* state) override {
		Sync();
		if (control_ != nullptr) {
			return control_->GetState(timeout, state);
		}
		if (state == nullptr) {
			return E_POINTER;
		}
		const ma::Run run = ma::StateOf(intent_);
		*state = run == ma::Run::Running ? State_Running : run == ma::Run::Paused ? State_Paused : State_Stopped;
		return S_OK;
	}
	STDMETHODIMP RenderFile(BSTR file) override { return PassControl([&](IMediaControl* c) { return c->RenderFile(file); }); }
	STDMETHODIMP AddSourceFilter(BSTR file, IDispatch** out) override { return PassControl([&](IMediaControl* c) { return c->AddSourceFilter(file, out); }); }
	STDMETHODIMP get_FilterCollection(IDispatch** out) override { return PassControl([&](IMediaControl* c) { return c->get_FilterCollection(out); }); }
	STDMETHODIMP get_RegFilterCollection(IDispatch** out) override {
		return PassControl([&](IMediaControl* c) { return c->get_RegFilterCollection(out); });
	}
	STDMETHODIMP StopWhenReady() override { return PassControl([&](IMediaControl* c) { return c->StopWhenReady(); }); }

	// IMediaPosition: the position is kept until the real graph is there.
	STDMETHODIMP put_CurrentPosition(REFTIME time) override {
		Sync();
		if (position_ != nullptr) {
			return position_->put_CurrentPosition(time);
		}
		ma::AskPosition(intent_, time);
		return S_OK;
	}
	STDMETHODIMP get_CurrentPosition(REFTIME* time) override {
		Sync();
		if (position_ != nullptr) {
			return position_->get_CurrentPosition(time);
		}
		if (time == nullptr) {
			return E_POINTER;
		}
		*time = intent_.position;
		return S_OK;
	}
	STDMETHODIMP get_Duration(REFTIME* out) override { return PassPosition([&](IMediaPosition* p) { return p->get_Duration(out); }); }
	STDMETHODIMP get_StopTime(REFTIME* out) override { return PassPosition([&](IMediaPosition* p) { return p->get_StopTime(out); }); }
	STDMETHODIMP put_StopTime(REFTIME time) override { return PassPosition([&](IMediaPosition* p) { return p->put_StopTime(time); }); }
	STDMETHODIMP get_PrerollTime(REFTIME* out) override { return PassPosition([&](IMediaPosition* p) { return p->get_PrerollTime(out); }); }
	STDMETHODIMP put_PrerollTime(REFTIME time) override { return PassPosition([&](IMediaPosition* p) { return p->put_PrerollTime(time); }); }
	STDMETHODIMP put_Rate(double rate) override { return PassPosition([&](IMediaPosition* p) { return p->put_Rate(rate); }); }
	STDMETHODIMP get_Rate(double* rate) override { return PassPosition([&](IMediaPosition* p) { return p->get_Rate(rate); }); }
	STDMETHODIMP CanSeekForward(LONG* can) override { return PassPosition([&](IMediaPosition* p) { return p->CanSeekForward(can); }); }
	STDMETHODIMP CanSeekBackward(LONG* can) override { return PassPosition([&](IMediaPosition* p) { return p->CanSeekBackward(can); }); }

	// IMediaEvent: no event until the real graph is there; one EC_ERRORABORT for a graph that could not be built.
	STDMETHODIMP GetEvent(long* code, LONG_PTR* first, LONG_PTR* second, long timeout) override {
		Sync();
		const ma::EventAnswer answer = ma::AnswerEvent(event_ != nullptr ? ma::Phase::Ready : PhaseNow(), &errorAnswered_);
		if (answer.forward) {
			return event_ != nullptr ? event_->GetEvent(code, first, second, timeout) : E_ABORT;  // a real graph without IMediaEvent: no event
		}
		if (!answer.event || code == nullptr || first == nullptr || second == nullptr) {
			return E_ABORT;
		}
		*code = answer.code;
		*first = 0;
		*second = 0;
		return S_OK;
	}
	STDMETHODIMP FreeEventParams(long code, LONG_PTR first, LONG_PTR second) override {
		Sync();
		return event_ != nullptr ? event_->FreeEventParams(code, first, second) : S_OK;
	}
	STDMETHODIMP GetEventHandle(OAEVENT* out) override { return PassEvent([&](IMediaEvent* e) { return e->GetEventHandle(out); }); }
	STDMETHODIMP WaitForCompletion(long timeout, long* code) override { return PassEvent([&](IMediaEvent* e) { return e->WaitForCompletion(timeout, code); }); }
	STDMETHODIMP CancelDefaultHandling(long code) override { return PassEvent([&](IMediaEvent* e) { return e->CancelDefaultHandling(code); }); }
	STDMETHODIMP RestoreDefaultHandling(long code) override { return PassEvent([&](IMediaEvent* e) { return e->RestoreDefaultHandling(code); }); }

private:
	~MusicGraph() {
		for (IUnknown* held : {static_cast<IUnknown*>(event_), static_cast<IUnknown*>(position_), static_cast<IUnknown*>(control_),
		                       static_cast<IUnknown*>(audio_), static_cast<IUnknown*>(real_)}) {
			if (held != nullptr) {
				held->Release();
			}
		}
	}

	void* Self(REFIID iid) {
		if (iid == IID_IUnknown || iid == __uuidof(IGraphBuilder) || iid == __uuidof(IFilterGraph)) {
			return static_cast<IGraphBuilder*>(this);
		}
		if (iid == __uuidof(IBasicAudio)) {
			return static_cast<IBasicAudio*>(this);
		}
		if (iid == __uuidof(IMediaControl)) {
			return static_cast<IMediaControl*>(this);
		}
		if (iid == __uuidof(IMediaPosition)) {
			return static_cast<IMediaPosition*>(this);
		}
		if (iid == __uuidof(IMediaEvent)) {
			return static_cast<IMediaEvent*>(this);
		}
		return nullptr;
	}

	ma::Phase PhaseNow() const { return phase_.load(std::memory_order_acquire); }

	// The first call on the main thread after the worker is done takes the real graph's interfaces and gives it what
	// the game asked so far, in the game's own order.
	void Sync() {
		if (adopted_ || PhaseNow() == ma::Phase::Building) {
			return;
		}
		adopted_ = true;
		if (real_ != nullptr) {
			real_->QueryInterface(__uuidof(IBasicAudio), reinterpret_cast<void**>(&audio_));
			real_->QueryInterface(__uuidof(IMediaControl), reinterpret_cast<void**>(&control_));
			real_->QueryInterface(__uuidof(IMediaPosition), reinterpret_cast<void**>(&position_));
			real_->QueryInterface(__uuidof(IMediaEvent), reinterpret_cast<void**>(&event_));
			Apply();
		} else {
			g_failed.fetch_add(1, std::memory_order_relaxed);
		}
		Report();
	}

	void Apply() {
		const ma::Plan plan = ma::PlanOf(intent_);
		if (plan.volume && audio_ != nullptr) {
			audio_->put_Volume(intent_.volume);
		}
		if (plan.position && position_ != nullptr) {
			position_->put_CurrentPosition(intent_.position);
		}
		if (control_ != nullptr) {
			if (plan.run) {
				control_->Run();
			} else if (plan.pause) {
				control_->Pause();
			} else if (plan.stop) {
				control_->Stop();
			}
		}
	}

	void ReportLine(const char* what, unsigned long result, long state) const {
		if (g_lines.fetch_add(1, std::memory_order_relaxed) >= kTrackLines) {
			return;
		}
		char path[MAX_PATH] = {};
		WideCharToMultiByte(CP_ACP, 0, path_, -1, path, sizeof(path) - 1, nullptr, nullptr);
		char line[160];
		wsprintfA(line, what, result, state);
		g_log("Route: music async - \"%s\" %s", path, line);
	}

	void Report() const {
		if (g_lines.fetch_add(1, std::memory_order_relaxed) >= kTrackLines) {
			return;
		}
		char path[MAX_PATH] = {};
		WideCharToMultiByte(CP_ACP, 0, path_, -1, path, sizeof(path) - 1, nullptr, nullptr);
		OAFilterState state = -1;
		if (control_ != nullptr) {
			control_->GetState(0, &state);
		}
		g_log("Route: music async - \"%s\" built on the worker in %.1f ms (%08lX), given to the game's graph %.1f ms after its RenderFile; "
		      "volume %s, position %s, run state %s; the graph's state now %ld",
		      path, Ms(buildTicks_), static_cast<unsigned long>(buildResult_), Ms(Now() - askedTick_), intent_.hasVolume ? "given" : "-",
		      intent_.hasPosition ? "given" : "-", intent_.runAsked ? (intent_.run == ma::Run::Running ? "run" : intent_.run == ma::Run::Paused ? "pause" : "stop") : "-",
		      static_cast<long>(state));
	}

	// The real graph for a call the game never makes: waits for the worker (counted) when it is still building.
	IGraphBuilder* Real() {
		if (!queued_ && real_ == nullptr && PhaseNow() == ma::Phase::Building) {
			return nullptr;  // nothing rendered yet: no real graph to ask
		}
		if (PhaseNow() == ma::Phase::Building) {
			g_waits.fetch_add(1, std::memory_order_relaxed);
			while (PhaseNow() == ma::Phase::Building) {
				Sleep(1);
			}
		}
		Sync();
		return real_;
	}

	template <typename F> HRESULT Pass(F call) {
		IGraphBuilder* real = Real();
		return real != nullptr ? call(real) : E_FAIL;
	}
	template <typename F> HRESULT PassAudio(F call) {
		Real();
		return audio_ != nullptr ? call(audio_) : E_FAIL;
	}
	template <typename F> HRESULT PassControl(F call) {
		Real();
		return control_ != nullptr ? call(control_) : E_FAIL;
	}
	template <typename F> HRESULT PassPosition(F call) {
		Real();
		return position_ != nullptr ? call(position_) : E_FAIL;
	}
	template <typename F> HRESULT PassEvent(F call) {
		Real();
		return event_ != nullptr ? call(event_) : E_FAIL;
	}

	HRESULT AskOrPass(ma::Run run) {
		Sync();
		if (control_ != nullptr) {
			const HRESULT result = run == ma::Run::Running ? control_->Run() : run == ma::Run::Paused ? control_->Pause() : control_->Stop();
			if (run == ma::Run::Running && !runPassedReported_ && !intent_.runAsked) {
				runPassedReported_ = true;  // a track the game runs only after the handover (its start during a fade)
				OAFilterState state = -1;
				control_->GetState(0, &state);
				ReportLine("run by the game after the handover (%08lX); the graph's state now %ld", static_cast<unsigned long>(result), static_cast<long>(state));
			}
			return result;
		}
		ma::AskRun(intent_, run);
		return S_OK;
	}

	std::atomic<long> refs_{1};
	std::atomic<ma::Phase> phase_{ma::Phase::Building};
	IGraphBuilder* real_ = nullptr;  // written by the worker before phase_ leaves Building
	HRESULT buildResult_ = S_OK;
	LONGLONG buildTicks_ = 0;
	LONGLONG askedTick_ = 0;
	wchar_t path_[MAX_PATH] = {};
	bool queued_ = false;
	// The main thread's own.
	bool adopted_ = false;
	bool errorAnswered_ = false;
	bool runPassedReported_ = false;
	ma::Intent intent_;
	IBasicAudio* audio_ = nullptr;
	IMediaControl* control_ = nullptr;
	IMediaPosition* position_ = nullptr;
	IMediaEvent* event_ = nullptr;
};

// The worker: one thread in COM's multithreaded apartment, the graphs built in the order asked.
CRITICAL_SECTION g_queueLock;
HANDLE g_queueEvent = nullptr;
MusicGraph* g_queue[kQueue];
UInt32 g_head = 0, g_tail = 0;

void Enqueue(MusicGraph* graph) {
	EnterCriticalSection(&g_queueLock);
	const bool full = g_tail - g_head >= kQueue;
	if (!full) {
		g_queue[g_tail++ % kQueue] = graph;
	}
	LeaveCriticalSection(&g_queueLock);
	if (full) {
		graph->Build();  // sixteen tracks waiting: built here, as the game would
		return;
	}
	SetEvent(g_queueEvent);
}

MusicGraph* Take() {
	EnterCriticalSection(&g_queueLock);
	MusicGraph* graph = g_head != g_tail ? g_queue[g_head++ % kQueue] : nullptr;
	LeaveCriticalSection(&g_queueLock);
	return graph;
}

DWORD WINAPI Worker(void*) {
	const auto initialize = reinterpret_cast<InitializeExFn>(GetProcAddress(GetModuleHandleA("ole32.dll"), "CoInitializeEx"));
	if (initialize != nullptr) {
		initialize(nullptr, COINIT_MULTITHREADED);
	}
	for (;;) {
		WaitForSingleObject(g_queueEvent, INFINITE);
		while (MusicGraph* graph = Take()) {
			graph->Build();
		}
	}
}

// In place of the music player's CoCreateInstance calls: a filter graph asked for as IGraphBuilder is the stand-in.
HRESULT __stdcall Create(const CLSID* clsid, IUnknown* outer, DWORD context, const IID* iid, void** out) {
	if (out != nullptr && outer == nullptr && clsid != nullptr && iid != nullptr && *clsid == kFilterGraph && *iid == __uuidof(IGraphBuilder)) {
		*out = static_cast<IGraphBuilder*>(new MusicGraph());
		return S_OK;
	}
	return g_create(clsid, outer, context, iid, out);
}

double TicksPerMs() {
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	return static_cast<double>(frequency.QuadPart) / 1000.0;
}

}  // namespace

void InstallMusicAsync(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (ma::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "MusicAsync", 0, iniPath))) == ma::Mode::Off) {
		return;
	}
	g_log = log;
	for (const UInt32 site : kSites) {
		if (!Verify(site, kSiteBytes, sizeof(kSiteBytes))) {
			log("Route: music async - the call at %08X is not the game's own; not routed", site);
			return;
		}
	}
	g_create = *reinterpret_cast<CreateFn*>(kCoCreateInstanceSlot);
	g_ticksPerMs = TicksPerMs();
	InitializeCriticalSection(&g_queueLock);
	g_queueEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
	HANDLE thread = g_queueEvent != nullptr ? CreateThread(nullptr, 0, &Worker, nullptr, 0, nullptr) : nullptr;
	if (thread == nullptr) {
		log("Route: music async - no worker thread; not routed");
		return;
	}
	CloseHandle(thread);
	unsigned patched = 0;
	for (const UInt32 site : kSites) {
		UInt8 patch[sizeof(kSiteBytes)] = {0xE8, 0, 0, 0, 0, 0x90};
		const UInt32 rel = reinterpret_cast<UInt32>(&Create) - (site + 5);
		std::memcpy(patch + 1, &rel, 4);
		patched += SafeWrite(site, patch, sizeof(patch)) ? 1 : 0;
	}
	g_on = patched != 0;
	log("Route: music async - %u of 7 track starts of the music player hand out a stand-in graph; the real one is built on a worker thread",
	    patched);
}

void LogMusicAsyncSummary() {
	if (!g_on) {
		return;
	}
	g_log("Route: music async - %u tracks started off the main thread, %u could not be built, %u calls waited for the worker",
	      g_tracks.exchange(0), g_failed.exchange(0), g_waits.exchange(0));
}

}  // namespace obng::plugin
