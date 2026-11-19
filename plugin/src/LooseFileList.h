#pragma once

// [Route] LooseFileList: FileFinder's search slot 0x00431800 answered from a listing of every file and folder under the
// game's folder, read once on a thread of its own at start (src/engine/io/LooseFileListLogic.h) - the operating
// system's file questions (_access) of a FindFile replaced by a lookup in memory. Only asset kinds are answered; INIs,
// text, logs and anything Windows would rewrite go to the original, as does every call before the listing is ready.
// 0 off, 1 the route, 2 the check: the listing's answer and the original's compared for every call (class A's gate).

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallLooseFileList(const char* iniPath, LogFn log);
void LogLooseFileListSummary();

}  // namespace obng::plugin
