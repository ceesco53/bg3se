#pragma once

#include <GameDefinitions/Base/WindowsSlim.h>

#include <memory>
#include <cstdint>
#include <array>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <sstream>
#include <cassert>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <variant>
#include <span>
#include <algorithm>

#if defined(__APPLE__)
#include <macOS/Platform/Darwin.h>
#include <macOS/Platform/FaultGuard.h>
#endif

#include <Extender/BuildInfo.h>
#include <Extender/Shared/Utils.h>
#include <GameDefinitions/Base/Base.h>
#include <Extender/Shared/Optick.h>
