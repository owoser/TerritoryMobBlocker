#pragma once

#include "ll/api/mod/NativeMod.h"

namespace tmb {

class Entry {
public:
    static Entry& getInstance();

    Entry();

    [[nodiscard]] ll::mod::NativeMod& getSelf() const;

    /// @return True if the mod is loaded successfully.
    bool load();

    /// @return True if the mod is enabled successfully.
    bool enable();

    /// @return True if the mod is disabled successfully.
    bool disable();

    /// @return True if the mod is unloaded successfully.
    bool unload();

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace tmb
