#include "Interceptor/ActorAddHook.h"

#include "Config/ConfigManager.h"
#include "Interceptor/SpawnJudge.h"
#include "logger.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/game_refs/OwnerPtr.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tmb {

namespace {

/**
 * 所有实体创建路径的公共汇合点。
 *
 * 采用后置挂钩：先 origin() 让引擎完成创建，拿到 Actor* 后再判定，
 * 避免解析 EntityContext；命中时只入队，实际清除推迟到下一 tick。
 */
LL_TYPE_INSTANCE_HOOK(
    LevelAddEntityHook,
    HookPriority::Normal,
    Level,
    &Level::$addEntity,
    Actor*,
    BlockSource&            region,
    OwnerPtr<EntityContext> entity
) {
    Actor* actor = origin(region, std::move(entity));
    if (actor) {
        ActorAddHook::getInstance().onActorCreated(actor);
    }
    return actor;
}

/// 待清理实体
struct PendingRemoval {
    ActorUniqueID id{};
    int           ticksLeft{1};
    std::string   mobId;
    std::string   landName;
    int64_t       landId{-1};
    BlockPos      pos{};
    int           dimId{0};
};

} // namespace

struct ActorAddHook::Impl {
    bool mEnabled{false};

    std::optional<ll::memory::HookRegistrar<LevelAddEntityHook>> hook;
    ll::event::ListenerPtr                                       tickListener;

    std::vector<PendingRemoval> pending;
};

ActorAddHook& ActorAddHook::getInstance() {
    static ActorAddHook instance;
    return instance;
}

ActorAddHook::ActorAddHook() : mImpl(std::make_unique<Impl>()) {}

ActorAddHook::~ActorAddHook() { disable(); }

bool ActorAddHook::isEnabled() const { return mImpl && mImpl->mEnabled; }

bool ActorAddHook::enable() {
    if (!mImpl) {
        mImpl = std::make_unique<Impl>();
    }
    if (mImpl->mEnabled) {
        return true;
    }

    try {
        mImpl->hook.emplace(); // HookRegistrar 构造即安装
    } catch (std::exception const& e) {
        logger.error("安装实体创建挂钩失败：{}（脚本生成路径将不被拦截）", e.what());
        mImpl->hook.reset();
        return false;
    } catch (...) {
        logger.error("安装实体创建挂钩失败：未知异常（脚本生成路径将不被拦截）");
        mImpl->hook.reset();
        return false;
    }

    mImpl->tickListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
        [](ll::event::ServerLevelTickEvent& ev) { ActorAddHook::getInstance().onLevelTick(&ev.level()); }
    );

    mImpl->mEnabled = true;
    logger.info("已安装实体创建挂钩：脚本生成（dimension.spawnEntity）的生物将一并纳入拦截");
    return true;
}

bool ActorAddHook::disable() {
    if (!mImpl || !mImpl->mEnabled) {
        return true;
    }

    if (mImpl->tickListener) {
        ll::event::EventBus::getInstance().removeListener<ll::event::ServerLevelTickEvent>(mImpl->tickListener);
        mImpl->tickListener = nullptr;
    }

    mImpl->pending.clear();
    mImpl->hook.reset(); // HookRegistrar 析构即卸载
    mImpl->mEnabled = false;
    return true;
}

void ActorAddHook::onActorCreated(void* actorPtr) {
    auto* actor = static_cast<Actor*>(actorPtr);
    if (!mImpl || !mImpl->mEnabled || !actor) {
        return;
    }

    auto const& cfg = ConfigManager::getInstance().get();
    if (!cfg.enabled || !cfg.hook_actor_add) {
        return;
    }

    // 玩家永远不处理
    if (actor->isPlayer()) {
        return;
    }

    // 已驯服生物默认跳过，避免清掉玩家的宠物
    if (cfg.hook_skip_tamed && actor->isTame()) {
        return;
    }

    auto const& typeName = actor->getTypeName();

    // Debug 级：记录每一次实体创建，便于确认挂钩是否生效
    if (cfg.log_level >= 4) {
        logger.debug("[ActorAdd] 实体创建: {}", typeName);
    }

    auto verdict = judgeSpawn(typeName, toBlockPos(actor->getPosition()), actor->getDimensionId());
    if (!verdict.block) {
        return;
    }

    int delay = cfg.hook_remove_delay_ticks;
    if (delay < 1) {
        delay = 1;
    }

    PendingRemoval item;
    item.id        = actor->getOrCreateUniqueID();
    item.ticksLeft = delay;
    item.mobId     = verdict.mobId;
    item.landName  = verdict.landName;
    item.landId    = verdict.landId;
    item.pos       = toBlockPos(actor->getPosition());
    item.dimId     = actor->getDimensionId();

    mImpl->pending.push_back(std::move(item));
}

void ActorAddHook::onLevelTick(void* levelPtr) {
    if (!mImpl || mImpl->pending.empty()) {
        return;
    }

    auto* level = static_cast<Level*>(levelPtr);
    if (!level) {
        return;
    }

    auto const& cfg = ConfigManager::getInstance().get();

    for (auto it = mImpl->pending.begin(); it != mImpl->pending.end();) {
        if (--it->ticksLeft > 0) {
            ++it;
            continue;
        }

        Actor* actor = level->fetchEntity(it->id, false);
        if (actor && actor->isAlive()) {
            // 二次确认：延迟期间生物可能已被驯服或已被移除
            bool skip = actor->isPlayer() || (cfg.hook_skip_tamed && actor->isTame());
            if (!skip) {
                actor->despawn();
                if (cfg.log_blocked_spawns) {
                    logger.info(
                        "已拦截领地「{}」(#{} 维度{}) 内通过脚本等方式生成的生物: {} @({}, {}, {})",
                        it->landName,
                        it->landId,
                        it->dimId,
                        it->mobId,
                        it->pos.x,
                        it->pos.y,
                        it->pos.z
                    );
                }
            }
        }

        it = mImpl->pending.erase(it);
    }
}

} // namespace tmb
