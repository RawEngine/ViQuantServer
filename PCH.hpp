
#pragma once

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <utility>
#include <queue>
#include <mutex>
#include <iostream> // std::cout
#include <condition_variable>
#include <unordered_map>

#include "Types.hpp"

#ifndef MAX_PATH
#define MAX_PATH 1024
#endif

#ifndef BIT
#define BIT(x) (1 << x)
#endif

#include "Exception.hpp"
#include "Log/Log.hpp"
