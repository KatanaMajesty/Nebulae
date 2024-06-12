#pragma once

#include "Defines.h"
#include <format>
#include <iostream>

#if defined(NEB_DEBUG)
#define NEB_LOG_INFO(msg, ...) (std::println(std::cout, msg, ##__VA_ARGS__))
#define NEB_LOG_WARN(msg, ...) (std::println(std::cout, msg, ##__VA_ARGS__))
#define NEB_LOG_ERROR(msg, ...) (std::println(std::cout, msg, ##__VA_ARGS__))
#else
#define NEB_LOG_INFO(msg, ...)
#define NEB_LOG_WARN(msg, ...)
#define NEB_LOG_ERROR(msg, ...)
#endif