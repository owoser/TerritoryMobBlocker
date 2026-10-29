#include "Interceptor/MobSpawnInterceptor.h"

#include "Config/ConfigManager.h"
#include "Interceptor/MobMatcher.h"
#include "Pland/PlandBridge.h"
#include "logger.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/SpawnMobEvent.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include <cmath>

namespace tmb {

namespace {

/// Vec3 -> 方块坐标（与 PLand 内部一致，使用 floor 而非截断）
BlockPos toBlockPos(Vec3 const& pos) {
    return BlockPos{
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y)),
        static_cast<int>(std::floor(pos.z))
    };
}

/// 判断本次生成是否需要处理（前置/后置开关）
bool shouldHandle(ll::event::SpawnMobEvent const& ev, Config const& config) {
    if (!config.enabled) {
        return false;
    }
    if (ev.naturalSpawn()) {
        return config.intercept_natural_spawn;
    }
    return config.intercept_non_natural_spawn;
}

} // namespace

MobSpawnInterceptor& MobSpawnInterceptor::getInstance() {
    static MobSpawnInterceptor instance;
    return instance;
}

MobSpawnInterceptor::MobSpawnInterceptor() = default;

MobSpawnInterceptor::~MobSpawnInterceptor() { disable(); }

bool MobSpawnInterceptor::enable() {
    if (mEnabled) {
        return true;
    }

    auto& bus = ll::event::EventBus::getInstance();

    // ---------------------------------------------------------------------
    // 一、前置拦截：BedrockSpawner::spawnMob 执行前触发，cancel() 后不会创建实体
    //     —— 零实体、零网络包开销，是首选通道。
    // ---------------------------------------------------------------------
    mSpawningListener = bus.emplaceListener<ll::event::SpawningMobEvent>([](ll::event::SpawningMobEvent& ev) {
        auto const& config = ConfigManager::getInstance().get();
        if (!shouldHandle(ev, config)) {
            return;
        }

        auto const& fullName = ev.identifier().mFullName.get();
        auto&       matcher  = MobMatcher::getInstance();
        if (!matcher.isBlocked(fullName)) {
            return;
        }

        auto& bridge = pland::PlandBridge::getInstance();
        if (!bridge.ensureAvailable()) {
            return; // PLand 尚不可用（带冷却的惰性重探已由 ensureAvailable 负责）
        }

        BlockPos blockPos = toBlockPos(ev.pos());
        int      dimId     = static_cast<int>(ev.blockSource().getDimensionId());

        auto land = bridge.landAt(blockPos, dimId);
        if (!land || !land->exists) {
            return; // 荒野，放行
        }

        // 领地关闭「怪物生成」时，拦截配置名单内的生物
        if (!land->allowMonsterSpawn) {
            ev.cancel();
            if (config.log_blocked_spawns) {
                logger.info(
                    "已拦截领地「{}」(#{} 维度{}) 内的自定义生物生成: {} @({}, {}, {})",
                    land->name,
                    land->id,
                    dimId,
                    MobMatcher::normalizeIdentifier(fullName),
                    blockPos.x,
                    blockPos.y,
                    blockPos.z
                );
            }
        }
    });

    // ---------------------------------------------------------------------
    // 二、后置兜底：实体已经创建（刷怪蛋 / 指令 / 生成器 / 组件骑手等非标准路径，
    //     以及某些在 population 管线之外绕过前置事件的生成方式）
    // ---------------------------------------------------------------------
    mSpawnedListener = bus.emplaceListener<ll::event::SpawnedMobEvent>([](ll::event::SpawnedMobEvent& ev) {
        auto const& config = ConfigManager::getInstance().get();
        if (!shouldHandle(ev, config)) {
            return;
        }

        auto mob = ev.mob();
        if (!mob) {
            return;
        }

        auto const& typeName = mob->getTypeName();
        auto&       matcher  = MobMatcher::getInstance();
        if (!matcher.isBlocked(typeName)) {
            return;
        }

        auto& bridge = pland::PlandBridge::getInstance();
        if (!bridge.ensureAvailable()) {
            return;
        }

        BlockPos blockPos = toBlockPos(mob->getPosition());
        int      dimId     = mob->getDimensionId();

        auto land = bridge.landAt(blockPos, dimId);
        if (!land || !land->exists) {
            return;
        }

        if (!land->allowMonsterSpawn) {
            mob->despawn();
            if (config.log_blocked_spawns) {
                logger.info(
                    "已后置清除领地「{}」(#{} 维度{}) 内的自定义生物: {} @({}, {}, {})",
                    land->name,
                    land->id,
                    dimId,
                    MobMatcher::normalizeIdentifier(typeName),
                    blockPos.x,
                    blockPos.y,
                    blockPos.z
                );
            }
        }
    });

    mEnabled = true;
    logger.info("领地自定义生物生成拦截监听器已注册就绪");
    return true;
}

bool MobSpawnInterceptor::disable() {
    if (!mEnabled) {
        return true;
    }

    auto& bus = ll::event::EventBus::getInstance();
    if (mSpawningListener) {
        bus.removeListener<ll::event::SpawningMobEvent>(mSpawningListener);
        mSpawningListener = nullptr;
    }
    if (mSpawnedListener) {
        bus.removeListener<ll::event::SpawnedMobEvent>(mSpawnedListener);
        mSpawnedListener = nullptr;
    }

    mEnabled = false;
    return true;
}

} // namespace tmb
