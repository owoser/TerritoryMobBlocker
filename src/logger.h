#pragma once

#include "Entry/Entry.h"
#include "ll/api/io/Logger.h"

namespace tmb {
inline ll::io::Logger& logger = Entry::getInstance().getSelf().getLogger();
} // namespace tmb
