#pragma once

#define RAPIDJSON_PARSE_DEFAULT_FLAGS kParseCommentsFlag | kParseTrailingCommasFlag | kParseNanAndInfFlag | kParseIterativeFlag
#define RAPIDJSON_WRITE_DEFAULT_FLAGS kWriteNanAndInfFlag | kWriteNanAndInfNullFlag
#if defined(_M_X64) || defined(__x86_64__)
#define RAPIDJSON_SIMD
#define RAPIDJSON_SSE42
#endif
#define RAPIDJSON_HAS_STDSTRING 1

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>
