#ifndef STUMBLERLOGGING_H
#define STUMBLERLOGGING_H

#include "mozilla/Logging.h"

PRLogModuleInfo* GetLog();

#define STUMBLER_DBG(arg, ...)  MOZ_LOG(GetLog(), mozilla::LogLevel::Debug, ("STUMBLER - %s: " arg, __func__, ##__VA_ARGS__))
#define STUMBLER_LOG(arg, ...)  MOZ_LOG(GetLog(), mozilla::LogLevel::Info, ("STUMBLER - %s: " arg, __func__, ##__VA_ARGS__))
#define STUMBLER_ERR(arg, ...)  MOZ_LOG(GetLog(), mozilla::LogLevel::Error, ("STUMBLER -%s: " arg, __func__, ##__VA_ARGS__))

#endif