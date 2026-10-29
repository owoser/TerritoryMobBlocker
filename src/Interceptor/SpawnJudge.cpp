#include "Interceptor/SpawnJudge.h"

#include "Config/ConfigManager.h"
#include "Interceptor/MobMatcher.h"
#include "Pland/PlandBridge.h"
#include "logger.h"

namespace tmb {

SpawnVerdict judgeSpawn(std::string_view rawTypeName, BlockPos const& pos, int dimId) {
    SpawnVerdict verdict;

    // 1. 先做最便宜的名单匹配：绝大多数实体在这里就被放行，不触碰 PLand
    auto& matcher = MobMatcher::getInstance();
    if (!matcher.isBlocked(rawTypeName)) {
        return verdict;
    }

    // 2. 查询所在领地（接口不可用时按"不拦截"处理，由启动自检负责告警）
    auto& bridge = pland::PlandBridge::getInstance();
    if (!bridge.ensureAvailable()) {
        if (ConfigManager::getInstance().get().log_level >= 4) {
            logger.debug("[判定] {} @({}, {}, {}) 命中名单，但 PLand 接口不可用", rawTypeName, pos.x, pos.y, pos.z);
        }
        return verdict;
    }

    auto land = bridge.landAt(pos, dimId);
    if (!land || !land->exists) {
        if (ConfigManager::getInstance().get().log_level >= 4) {
            logger.debug(
                "[判定] {} @({}, {}, {}) dim={} 命中名单，但坐标不在任何领地内（荒野）",
                rawTypeName,
                pos.x,
                pos.y,
                pos.z,
                dimId
            );
        }
        return verdict; // 荒野
    }

    verdict.inLand   = true;
    verdict.mobId    = MobMatcher::normalizeIdentifier(rawTypeName);
    verdict.landName = land->name;
    verdict.landId   = land->id;

    // 3. 只有领地关闭「怪物生成」时才拦截
    verdict.block = !land->allowMonsterSpawn;

    if (ConfigManager::getInstance().get().log_level >= 4) {
        logger.debug(
            "[判定] {} @({}, {}, {}) dim={} → 领地「{}」#{} allowMonsterSpawn={} animalSpawn={} => {}",
            rawTypeName,
            pos.x,
            pos.y,
            pos.z,
            dimId,
            land->name,
            land->id,
            land->allowMonsterSpawn,
            land->allowAnimalSpawn,
            verdict.block ? "拦截" : "放行"
        );
    }

    return verdict;
}

} // namespace tmb

