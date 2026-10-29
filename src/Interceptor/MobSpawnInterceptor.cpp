#include "Interceptor/MobSpawnInterceptor.h"

#include "Config/ConfigManager.h"
#include "Interceptor/MobMatcher.h"
#include "Interceptor/SpawnJudge.h"
#include "logger.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/SpawnMobEvent.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

namespace tmb {

namespace {

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
    //     覆盖：自然刷怪 / 刷怪笼 / /summon / 刷怪蛋 / 组件骑手。
    // ---------------------------------------------------------------------
    mSpawningListener = bus.emplaceListener<ll::event::SpawningMobEvent>([](ll::event::SpawningMobEvent& ev) {
        auto const& config = ConfigManager::getInstance().get();
        if (!shouldHandle(ev, config)) {
            return;
        }

        auto const& fullName = ev.identifier().mFullName.get();
        auto&       matcher  = MobMatcher::getInstance();
        if (!matcher.isBlocked(fullName)) {
            return; // 名单外，放行（绝大多数情况，不做任何领地查询）
        }

        BlockPos blockPos = toBlockPos(ev.pos());
        int      dimId     = static_cast<int>(ev.blockSource().getDimensionId());

        auto verdict = judgeSpawn(fullName, blockPos, dimId);
        if (!verdict.block) {
            return; // 荒野或领地允许生成
        }

        ev.cancel();
        if (config.log_blocked_spawns) {
            logger.info(
                "已拦截领地「{}」(#{} 维度{}) 内的自定义生物生成: {} @({}, {}, {})",
                verdict.landName,
                verdict.landId,
                dimId,
                verdict.mobId,
                blockPos.x,
                blockPos.y,
                blockPos.z
            );
        }
    });

    // ---------------------------------------------------------------------
    // 二、后置兜底：同一挂钩点的「实体已创建」分支（SpawnedMobEvent）。
    //     少数路径会绕过前置事件，这里按需清理。
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

        BlockPos blockPos = toBlockPos(mob->getPosition());
        int      dimId     = mob->getDimensionId();

        auto verdict = judgeSpawn(typeName, blockPos, dimId);
        if (!verdict.block) {
            return;
        }

        mob->despawn();
        if (config.log_blocked_spawns) {
            logger.info(
                "已后置清除领地「{}」(#{} 维度{}) 内的自定义生物: {} @({}, {}, {})",
                verdict.landName,
                verdict.landId,
                dimId,
                verdict.mobId,
                blockPos.x,
                blockPos.y,
                blockPos.z
            );
        }
    });

    mEnabled = true;
    logger.info("生成事件监听器已注册就绪（自然刷怪 / 刷怪笼 / 指令 / 刷怪蛋）");
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
