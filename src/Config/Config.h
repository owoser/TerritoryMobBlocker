#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace tmb {

/// 当前配置文件结构版本号。修改 Config 字段或默认值时递增，并在 ConfigManager 中补迁移步骤。
inline constexpr int kCurrentConfigVersion = 3;

struct Config {
    /// 配置文件结构版本号（插件升级时会自动迁移并写回）
    int version = kCurrentConfigVersion;

    /// 插件总开关
    bool enabled = true;

    /**
     * 拦截名单：当领地关闭「环境权限 - 怪物生成」时，命中此名单的生物禁止在领地内生成。
     *
     * 支持四种写法（大小写不敏感）：
     *   "my_addon:mutant_creeper"   完整 ID
     *   "mutant_creeper"            简写，匹配任意命名空间
     *   "my_addon:*"                该命名空间下全部生物
     *   "boss_*"                    前缀匹配（任意命名空间）
     */
    std::vector<std::string> blocked_mobs = {
        "example_addon:custom_monster",
        "example_addon:boss_*",
    };

    /// 豁免名单：即使命中 blocked_mobs（或开启了一键非原版拦截）也强制放行
    std::vector<std::string> exempt_mobs = {};

    /// 一键拦截全部非原版生物（非 minecraft: 命名空间），免去逐条配置
    bool block_all_non_vanilla_mobs = false;

    /// 是否拦截自然生成（population 管线）
    bool intercept_natural_spawn = true;

    /// 是否拦截非自然生成（刷怪蛋 / 指令 / 刷怪笼 / 组件等）
    bool intercept_non_natural_spawn = true;

    /**
     * 是否挂钩底层「实体创建」以覆盖脚本生成的生物。
     *
     * LeviLamina 的生成事件只覆盖 BedrockSpawner::spawnMob（自然刷怪 / 刷怪笼 /
     * 指令 / 刷怪蛋），而脚本 API `dimension.spawnEntity()` 会绕过它。开启本项后，
     * 这类生物同样会被拦截。关掉则只保留事件通道。
     */
    bool hook_actor_add = true;

    /// 挂钩通道命中时是否跳过已驯服生物（避免清掉玩家的宠物）
    bool hook_skip_tamed = true;

    /// 挂钩通道命中后延迟多少 tick 清除（1 = 下一 tick）
    int hook_remove_delay_ticks = 1;

    /// 是否在控制台输出拦截日志（默认关闭，避免刷屏）
    bool log_blocked_spawns = false;

    /// 日志等级：-1 Off, 0 Fatal, 1 Error, 2 Warn, 3 Info, 4 Debug, 5 Trace
    int log_level = 3;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Config,
    version,
    enabled,
    blocked_mobs,
    exempt_mobs,
    block_all_non_vanilla_mobs,
    intercept_natural_spawn,
    intercept_non_natural_spawn,
    hook_actor_add,
    hook_skip_tamed,
    hook_remove_delay_ticks,
    log_blocked_spawns,
    log_level
)

} // namespace tmb
