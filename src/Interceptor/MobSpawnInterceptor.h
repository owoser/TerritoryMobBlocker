#pragma once

#include "ll/api/event/ListenerBase.h"

namespace tmb {

class MobSpawnInterceptor {
public:
    static MobSpawnInterceptor& getInstance();

    MobSpawnInterceptor();
    ~MobSpawnInterceptor();

    // 启用拦截监听
    bool enable();

    // 禁用并清理监听器
    bool disable();

    [[nodiscard]] bool isEnabled() const { return mEnabled; }

private:
    bool                   mEnabled{false};
    ll::event::ListenerPtr mSpawningListener{nullptr};
    ll::event::ListenerPtr mSpawnedListener{nullptr};
};

} // namespace tmb
