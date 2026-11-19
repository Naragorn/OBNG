#pragma once

// The listing of every file and folder under the game's working folder, made once on a thread of its own and shared
// by the routes that answer the operating system's file questions from it ([Route] LooseFileList, ArchiveLooseCheck;
// src/engine/io/LooseFileListLogic.h).

#include "ShadowTimeInfo.h"
#include "engine/io/LooseFileListLogic.h"

namespace obng::plugin {

// Starts the listing once (later calls do nothing and give the first one's answer); false when the working folder
// cannot be read.
bool StartLooseListing(LogFn log);
// Null until the listing is sealed; then the same listing for the process's life.
const obng::engine::io::loose_file_list::Listing* LooseListing();
const char* LooseListingRoot();
double LooseListingBuildMs();
unsigned LooseListingTwins();

}  // namespace obng::plugin
