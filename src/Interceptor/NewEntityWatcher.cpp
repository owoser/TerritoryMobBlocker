#include "Interceptor/NewEntityWatcher.h"

#include "Config/ConfigManager.h"
#include "Interceptor/MobMatcher.h"
#include "Interceptor/SpawnJudge.h"
#include "logger.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/Level.h"

#include <unordered_set>
#include <vector>

namespace tmb {

namespace {

/// ActorUniqueID 取出裸 ID 用于哈希
int64_t rawId(ActorUniqueID const& id) { return id.rawID; }

} // namespace

struct NewEntityWatcher::Impl {
    bool            mEnabled{false};
    bool            mBaselineReady{false};
    int             mTickCounter{0};
    ll::event::ListenerPtr listener;

    /// 已经"见过"的实体 ID：第一次出现即登记，之后不再处理
    std::unordered_set<int64_t> seen;

    size_t lastScanned{0};
    size_t lastNew{0};
    size_t lastRemoved{0};
};

NewEntityWatcher& NewEntityWatcher::getInstance() {
    static NewEntityWatcher instance;
    return instance;
}

NewEntityWatcher::NewEntityWatcher() : mImpl(std::make_unique<Impl>()) {}

NewEntityWatcher::~NewEntityWatcher() { disable(); }

bool NewEntityWatcher::isEnabled() const { return mImpl && mImpl->mEnabled; }

bool NewEntityWatcher::enable() {
    if (!mImpl) {
        mImpl = std::make_unique<Impl>();
    }
    if (mImpl->mEnabled) {
        return true;
    }

    mImpl->mBaselineReady = false;
    mImpl->mTickCounter   = 0;
    mImpl->seen.clear();

    mImpl->listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
        [](ll::event::ServerLevelTickEvent& ev) { NewEntityWatcher::getInstance().onLevelTick(&ev.level()); }
    );

    mImpl->mEnabled = true;
    logger.info("新实体监视器已启动：将以「新出现即判定」的方式覆盖所有实体创建路径");
    return true;
}

bool NewEntityWatcher::disable() {
    if (!mImpl || !mImpl->mEnabled) {
        return true;
    }
    if (mImpl->listener) {
        ll::event::EventBus::getInstance().removeListener<ll::event::ServerLevelTickEvent>(mImpl->listener);
        mImpl->listener = nullptr;
    }
    mImpl->seen.clear();
    mImpl->mEnabled = false;
    return true;
}

void NewEntityWatcher::onLevelTick(void* levelPtr) {
    if (!mImpl || !mImpl->mEnabled) {
        return;
    }

    auto const& cfg = ConfigManager::getInstance().get();
    if (!cfg.enabled || !cfg.watch_new_entities) {
        return;
    }

    int interval = cfg.watch_interval_ticks;
    if (interval < 1) {
        interval = 1;
    }
    if (++mImpl->mTickCounter < interval) {
        return;
    }
    mImpl->mTickCounter = 0;

    auto* level = static_cast<Level*>(levelPtr);
    if (!level) {
        return;
    }

    auto actors = level->getRuntimeActorList();

    // 第一轮只做基线：把当前已有的实体全部登记为"存量"，一个都不动
    if (!mImpl->mBaselineReady) {
        mImpl->seen.clear();
        for (auto* actor : actors) {
            if (actor) {
                mImpl->seen.insert(rawId(actor->getOrCreateUniqueID()));
            }
        }
        mImpl->mBaselineReady = true;
        mImpl->lastScanned    = actors.size();
        logger.info("新实体监视器：已登记 {} 个存量实体，此后只处理新出现的实体", actors.size());
        return;
    }

    std::unordered_set<int64_t> current;
    current.reserve(actors.size());

    size_t newCount     = 0;
    size_t removedCount = 0;

    for (auto* actor : actors) {
        if (!actor) {
            continue;
        }

        int64_t const id = rawId(actor->getOrCreateUniqueID());
        current.insert(id);

        // 已经见过的（存量或本轮之前出现过）一律不动
        if (mImpl->seen.contains(id)) {
            continue;
        }

        // 本轮新出现的实体
        ++newCount;

        if (actor->isPlayer()) {
            continue;
        }

        auto const& typeName = actor->getTypeName();
        auto const  pos      = toBlockPos(actor->getPosition());
        int const   dimId    = actor->getDimensionId();
        bool const  inList   = MobMatcher::getInstance().isBlocked(typeName);
        bool const  tamed    = actor->isTame();

        if (cfg.log_level >= 4 && (inList || cfg.watch_log_all_new_entities)) {
            logger.debug(
                "[新实体] {} @({}, {}, {}) dim={}{}{}",
                typeName,
                pos.x,
                pos.y,
                pos.z,
                dimId,
                inList ? " [名单内]" : "",
                tamed ? " [已驯服]" : ""
            );
        }

        if (!inList) {
            continue;
        }

        if (cfg.watch_skip_tamed && tamed) {
            if (cfg.log_level >= 4) {
                logger.debug("[新实体] 跳过 {}：已驯服（watch_skip_tamed=true）", typeName);
            }
            continue;
        }

        auto verdict = judgeSpawn(typeName, pos, dimId);
        if (!verdict.block) {
            continue;
        }

        actor->despawn();
        ++removedCount;

        if (cfg.log_blocked_spawns) {
            logger.info(
                "已拦截领地「{}」(#{} 维度{}) 内的自定义生物: {} @({}, {}, {})",
                verdict.landName,
                verdict.landId,
                dimId,
                verdict.mobId,
                pos.x,
                pos.y,
                pos.z
            );
        }
    }

    // 用当前实体列表重建集合：已消失的实体自然被淘汰，内存不会无限增长
    mImpl->seen       = std::move(current);
    mImpl->lastScanned = actors.size();
    mImpl->lastNew     = newCount;
    mImpl->lastRemoved = removedCount;
}

} // namespace tmb
