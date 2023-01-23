
#pragma once

//#include <cstdint>
//#include <limits>
/*

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <bitset>
#include <unordered_map>
*/

using I8 = int8_t;
const I8 I8_MAX = std::numeric_limits<I8>::max();
const I8 I8_MIN = std::numeric_limits<I8>::min();

using I16 = int16_t;
const I16 I16_MAX = std::numeric_limits<I16>::max();
const I16 I16_MIN = std::numeric_limits<I16>::min();

using I32 = int32_t;
const I32 I32_MAX = std::numeric_limits<I32>::max();
const I32 I32_MIN = std::numeric_limits<I32>::min();

using I64 = int64_t;
const I64 I64_MAX = std::numeric_limits<I64>::max();
const I64 I64_MIN = std::numeric_limits<I64>::min();


using U8 = uint8_t;
const U8 U8_MAX = std::numeric_limits<U8>::max();
const U8 U8_MIN = std::numeric_limits<U8>::min();

using U16 = uint16_t;
const U16 U16_MAX = std::numeric_limits<U16>::max();
const U16 U16_MIN = std::numeric_limits<U16>::min();

using U32 = uint32_t;
const U32 U32_MAX = std::numeric_limits<U32>::max();
const U32 U32_MIN = std::numeric_limits<U32>::min();

using U64 = uint64_t;
const U64 U64_MAX = std::numeric_limits<U64>::max();
const U64 U64_MIN = std::numeric_limits<U64>::min();


using F32 = float;
const F32 F32_MAX = std::numeric_limits<F32>::max();
const F32 F32_MIN = std::numeric_limits<F32>::min();

using F64 = double;
const F64 F64_MAX = std::numeric_limits<F64>::max();
const F64 F64_MIN = std::numeric_limits<F64>::min();

using String = std::string;
using StringW = std::wstring;

template<typename T>
using Vector = std::vector<T>;

//template<typename A, typename B>
//using Map = std::map<A, B>;

template<typename A, typename B>
using UnorderedMap = std::unordered_map < A, B >;

template<typename T>
using UniquePtr = std::unique_ptr<T>;

using EventId = U64; // Id of the EVENT.
using EventFootageId = U64; // Id of the footage that is associated with the EVENT (EventId)

// Event session id
// When FTP client connect to the server [for the first time] and gets validated (using valid username + password)
// This FTP client will be assigned with the unique "Event session id".
// If client is not validated, NO "Event session" will be created and thus there will be no "Event session id".
using EventSessionId = U32;

constexpr auto InvalidEventId = U64_MAX;
constexpr auto InvalidEventSessionId = U32_MAX;

using AnalyticsSessionId = U32;

using ClientId = U32; // TODO: Rename to FTPClientId ?

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>; // 8 bytes?

#ifdef PLATFORM_WINDOWS
using SocketId = SOCKET;
#else
using SocketId = int;
#endif

#ifndef PLATFORM_WINDOWS
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (SocketId)(~0)
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#endif
