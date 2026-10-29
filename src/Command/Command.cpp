#include "Command/Command.h"

#include "Config/ConfigManager.h"
#include "Interceptor/ActorAddHook.h"
#include "Interceptor/NewEntityWatcher.h"
#include "Interceptor/MobMatcher.h"
#include "Interceptor/SpawnJudge.h"
#include "Pland/PlandBridge.h"
#include "logger.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/Actor.h"
#include "ll/api/service/Bedrock.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"

#include <cmath>

namespace tmb {

/**
 * /tmb test <mobId> 的参数
 *
 * 必须定义在具名命名空间（而非匿名命名空间）：ll::command 用 boost::pfr 做聚合反射，
 * PFR 需要该类型具有外部链接，否则会报 "used but not defined ... does not have linkage"。
 */
struct TestParam {
    std::string mobId;
};

/// /tmb sweep [radius] [skip_tamed]
struct SweepParam {
    int  radius{32};
    bool skip_tamed{true};
};

namespace {

/**
 * 全局 OP 校验（纵深防御）
 *
 * 命令本身已用 CommandPermissionLevel::GameDirectors 注册（等价于 /op 授予的
 * operator 权限），BDS 会在解析阶段就过滤掉无权限的玩家。这里再做一次显式校验，
 * 用于覆盖「某些服务端修改了权限等级映射」的情况，并给出明确提示。
 *
 * 注意：领地主人、PLand 管理员都不会获得额外放行 —— 本插件只认全局 OP。
 */
bool requireOperator(CommandOrigin const& origin, CommandOutput& output) {
    if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
        output.error("§c[TMB] 该指令仅限全局 OP 使用。");
        return false;
    }
    return true;
}

void doReload(CommandOutput& output) {
    auto& manager = ConfigManager::getInstance();
    if (!manager.reload()) {
        output.error("§c[TMB] 配置重载失败，请检查 config.jsonc 的语法（多余逗号 / 缺少引号等）！");
        return;
    }

    auto const& cfg = manager.get();
    MobMatcher::getInstance().updateRules(cfg.blocked_mobs, cfg.exempt_mobs, cfg.block_all_non_vanilla_mobs);

    output.success(
        "§a[TMB] 配置重载成功：拦截 {} 条 / 豁免 {} 条，一键非原版拦截 {}",
        MobMatcher::getInstance().blockRuleCount(),
        MobMatcher::getInstance().exemptRuleCount(),
        cfg.block_all_non_vanilla_mobs ? "§a开启" : "§c关闭"
    );
    // 注意：此处为 Info 级日志，需 log_level >= 3 才会输出
    logger.info(
        "管理员重载配置完成，拦截规则 {} 条，豁免规则 {} 条",
        MobMatcher::getInstance().blockRuleCount(),
        MobMatcher::getInstance().exemptRuleCount()
    );
}

void doList(CommandOutput& output) {
    auto const& cfg = ConfigManager::getInstance().get();
    output.success("§e===== TerritoryMobBlocker 运行状态 =====");
    output.success("插件开关: {}", cfg.enabled ? "§a开启" : "§c关闭");
    output.success("一键拦截全部非原版生物: {}", cfg.block_all_non_vanilla_mobs ? "§a开启" : "§c关闭");
    output.success("拦截自然生成: {}", cfg.intercept_natural_spawn ? "§a开启" : "§c关闭");
    output.success("拦截非自然生成: {}", cfg.intercept_non_natural_spawn ? "§a开启" : "§c关闭");
    output.success(
        "生成路径挂钩(Level::$addEntity): {}",
        ActorAddHook::getInstance().isEnabled() ? "§a已安装" : (cfg.hook_actor_add ? "§c配置已开但安装失败" : "§7已按配置关闭")
    );
    output.success(
        "新实体监视器(兜底, 与路径无关): {}",
        NewEntityWatcher::getInstance().isEnabled() ? "§a运行中" : (cfg.watch_new_entities ? "§c配置已开但未启动" : "§7已按配置关闭")
    );
    output.success("拦截日志: {}", cfg.log_blocked_spawns ? "§a开启" : "§c关闭");

    auto& bridge = pland::PlandBridge::getInstance();
    if (bridge.isAvailable()) {
        output.success("PLand 接口: §a可用 §7(v{})", bridge.version());
    } else {
        output.error("PLand 接口: §c不可用（当前不会拦截任何生物）§r —— 执行 §e/tmb probe§r 查看自检报告");
    }

    output.success("黑名单条目 ({}):", cfg.blocked_mobs.size());
    for (auto const& mob : cfg.blocked_mobs) {
        output.success("  §b{}§r", mob);
    }
    if (!cfg.exempt_mobs.empty()) {
        output.success("豁免条目 ({}) —— 命中黑名单也放行:", cfg.exempt_mobs.size());
        for (auto const& mob : cfg.exempt_mobs) {
            output.success("  §a{}§r", mob);
        }
    }
}

void doProbe(CommandOutput& output) {
    auto& bridge = pland::PlandBridge::getInstance();
    bool  ok     = bridge.probe();

    output.success("§e===== PLand 接口自检 =====");
    for (auto const& item : bridge.report()) {
        output.success(
            "{} {}  §7{}",
            item.ok ? "§a[OK]  " : "§c[FAIL]",
            item.label,
            item.symbol.empty() ? "-" : item.symbol
        );
    }
    if (ok) {
        output.success("§a自检通过，PLand v{} 接口全部可用。", bridge.version());
        output.success("§7环境权限字段布局会在首次读取领地时自动校验。");
    } else {
        output.error("§c自检未通过：PLand 未安装、被禁用或导出符号不匹配，本插件将不拦截任何生物。");
    }
}

void doTest(CommandOrigin const& origin, CommandOutput& output, TestParam const& param) {
    auto& matcher = MobMatcher::getInstance();
    bool  hit     = matcher.matchesBlockList(param.mobId);
    bool  exempt  = matcher.isExempt(param.mobId);

    output.success("§e[TMB] 测试生物 ID: §f{}", param.mobId);
    output.success("黑名单匹配: {}", hit ? "§c命中" : "§a未命中");
    if (exempt) {
        output.success("豁免匹配: §a命中（将强制放行）");
    }

    auto entity = origin.getEntity();
    if (!entity) {
        output.success("§7(该指令需由玩家执行才能读取所在领地信息)");
        return;
    }

    auto& bridge = pland::PlandBridge::getInstance();
    if (!bridge.ensureAvailable()) {
        output.error("§cPLand 接口不可用，无法读取领地信息。执行 §e/tmb probe §c查看自检报告。");
        return;
    }

    auto const& pos      = entity->getPosition();
    BlockPos    blockPos{static_cast<int>(std::floor(pos.x)),
                         static_cast<int>(std::floor(pos.y)),
                         static_cast<int>(std::floor(pos.z))};
    int         dimId = entity->getDimensionId();

    auto land = bridge.landAt(blockPos, dimId);
    if (!land || !land->exists) {
        output.success(
            "当前位置 §7({}, {}, {})§r 维度 {} 不在任何领地内（荒野）。",
            blockPos.x,
            blockPos.y,
            blockPos.z,
            dimId
        );
        return;
    }

    output.success("所在领地: §b{}§r (#{})", land->name, land->id);
    output.success("环境权限 · 怪物生成: {}", land->allowMonsterSpawn ? "§a允许" : "§c禁止");
    output.success("环境权限 · 动物生成: {}", land->allowAnimalSpawn ? "§a允许" : "§c禁止");

    if (!bridge.isPermsLayoutValid()) {
        output.error("§c[警告] 环境权限字段布局校验失败，本插件已降级为不拦截，请检查 PLand 版本。");
        return;
    }

    if (!land->allowMonsterSpawn && hit && !exempt) {
        output.success("§c[判定] 该生物在此领地内会被 §l拦截§r§c。");
    } else if (!land->allowMonsterSpawn && exempt) {
        output.success("§a[判定] 命中豁免名单，强制放行。");
    } else if (!hit) {
        output.success("§a[判定] 未命中黑名单，不拦截。");
    } else {
        output.success("§e[判定] 命中黑名单，但该领地允许怪物生成，因此不拦截。");
    }
}

/**
 * 手动清理存量（只在你主动执行时生效，默认行为始终是"只拦生成"）
 *
 * 扫描半径内的实体，把「命中名单 + 所在领地禁止怪物生成」的实体清除。
 * 这是给"之前已经刷出来的生物"用的兜底手段，不会自动运行。
 */
void doSweep(CommandOrigin const& origin, CommandOutput& output, SweepParam const& param) {
    auto entity = origin.getEntity();
    if (!entity) {
        output.error("§c该指令需要由玩家执行（需要中心坐标）。");
        return;
    }

    auto* level = ll::service::getLevel().as_ptr();
    if (!level) {
        output.error("§c无法获取世界实例。");
        return;
    }

    int radius = param.radius;
    if (radius <= 0) {
        radius = 32;
    }
    if (radius > 256) {
        radius = 256;
    }

    auto const& center = entity->getPosition();
    int const   dimId  = entity->getDimensionId();
    double      r2     = static_cast<double>(radius) * radius;

    int scanned = 0;
    int matched = 0;
    int removed = 0;
    int skippedTamed = 0;

    auto actors = level->getRuntimeActorList();
    for (auto* actor : actors) {
        if (!actor || actor->isPlayer()) {
            continue;
        }
        if (actor->getDimensionId() != dimId) {
            continue;
        }

        auto const& pos = actor->getPosition();
        double      dx  = pos.x - center.x;
        double      dy  = pos.y - center.y;
        double      dz  = pos.z - center.z;
        if (dx * dx + dy * dy + dz * dz > r2) {
            continue;
        }

        ++scanned;

        auto const& typeName = actor->getTypeName();
        if (!MobMatcher::getInstance().isBlocked(typeName)) {
            continue;
        }
        ++matched;

        if (param.skip_tamed && actor->isTame()) {
            ++skippedTamed;
            continue;
        }

        auto verdict = judgeSpawn(typeName, toBlockPos(pos), dimId);
        if (!verdict.block) {
            continue;
        }

        actor->despawn();
        ++removed;
        logger.info(
            "手动清理：领地「{}」(#{} 维度{}) 内的 {} @({}, {}, {})",
            verdict.landName,
            verdict.landId,
            dimId,
            verdict.mobId,
            static_cast<int>(std::floor(pos.x)),
            static_cast<int>(std::floor(pos.y)),
            static_cast<int>(std::floor(pos.z))
        );
    }

    output.success("§e===== 手动清理结果（半径 {} 格）=====", radius);
    output.success("扫描实体: {}", scanned);
    output.success("命中名单: {}", matched);
    output.success(
        "已清除: {}  跳过已驯服: {}",
        removed,
        skippedTamed
    );
    output.success("§7(注：本插件默认只拦生成，不清扫存量；本次为你手动触发)");
}

} // namespace

void registerCommand() {
    auto& registrar = ll::command::CommandRegistrar::getInstance(false);

    auto& cmd = registrar.getOrCreateCommand(
        "tmb",
        "领地自定义生物生成拦截管理（全局 OP）",
        CommandPermissionLevel::GameDirectors
    );

    // 注意：ll::command 的 overload<T>() **不会**自动添加参数，
    // 必须显式调用 .text() / .required() / .optional() 来逐个声明。
    // 这里全部使用字面量子命令（而非 enum 参数），避免枚举注册时序问题。

    // /tmb —— 等同于 list
    cmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!requireOperator(origin, output)) return;
        doList(output);
    });

    // /tmb list
    cmd.overload().text("list").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!requireOperator(origin, output)) return;
        doList(output);
    });

    // /tmb reload
    cmd.overload().text("reload").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!requireOperator(origin, output)) return;
        doReload(output);
    });

    // /tmb probe
    cmd.overload().text("probe").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!requireOperator(origin, output)) return;
        doProbe(output);
    });

    // /tmb sweep [radius] [skip_tamed] —— 手动清理存量
    cmd.overload<SweepParam>()
        .text("sweep")
        .optional("radius")
        .optional("skip_tamed")
        .execute([](CommandOrigin const& origin, CommandOutput& output, SweepParam const& param) {
            if (!requireOperator(origin, output)) return;
            doSweep(origin, output, param);
        });

    // /tmb test <mobId>
    cmd.overload<TestParam>()
        .text("test")
        .required("mobId")
        .execute([](CommandOrigin const& origin, CommandOutput& output, TestParam const& param) {
            if (!requireOperator(origin, output)) return;
            doTest(origin, output, param);
        });
}

} // namespace tmb
