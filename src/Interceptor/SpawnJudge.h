#pragma once

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace tmb {

/// 一次生成判定的结论（只描述结果，不执行任何动作）
struct SpawnVerdict {
    bool        block{false};   ///< 是否应当拦截
    bool        inLand{false};  ///< 坐标是否位于某个领地内（荒野为 false）
    std::string mobId;          ///< 规范化后的实体 ID，如 "gronckle:gronckle"
    std::string landName;       ///< 命中时所在领地名称
    int64_t     landId{-1};     ///< 命中时所在领地 ID
};

/// Vec3 -> 方块坐标（与 PLand 内部一致，向下取整）
inline BlockPos toBlockPos(Vec3 const& pos) {
    return BlockPos{
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y)),
        static_cast<int>(std::floor(pos.z))
    };
}

/**
 * 三条拦截通道共用的判定逻辑
 *
 * 生成路径有三个入口：
 *   1. 前置 SpawningMobEvent      —— 自然刷怪 / 刷怪笼 / 指令 / 刷怪蛋
 *   2. 后置 SpawnedMobEvent       —— 同上路径的兜底
 *   3. actor-add 底层挂钩         —— 脚本 dimension.spawnEntity() 等绕过上两者的路径
 * 三者必须用同一套判定，避免行为不一致。
 *
 * @param rawTypeName 实体类型名（可带 `<event>` 后缀）
 * @param pos         生成位置（方块坐标）
 * @param dimId       维度 ID
 */
SpawnVerdict judgeSpawn(std::string_view rawTypeName, BlockPos const& pos, int dimId);

} // namespace tmb
