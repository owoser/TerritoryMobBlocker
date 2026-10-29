#include "Entry/Entry.h"

#include "Command/Command.h"
#include "Config/ConfigManager.h"
#include "Interceptor/ActorAddHook.h"
#include "Interceptor/MobMatcher.h"
#include "Interceptor/MobSpawnInterceptor.h"
#include "Pland/PlandBridge.h"
#include "ll/api/mod/RegisterHelper.h"
#include "logger.h"

#include <exception>
#include <filesystem>

namespace tmb {

Entry& Entry::getInstance() {
    static Entry instance;
    return instance;
}

Entry::Entry() : mSelf(*ll::mod::NativeMod::current()) {}

ll::mod::NativeMod& Entry::getSelf() const { return mSelf; }

bool Entry::load() {
    auto& log = getSelf().getLogger();
    log.info("正在加载 TerritoryMobBlocker（PLand 领地自定义生物生成拦截）...");

    auto configDir = getSelf().getConfigDir();
    std::error_code ec;
    if (!std::filesystem::exists(configDir, ec)) {
        std::filesystem::create_directories(configDir, ec);
    }
    auto configPath = configDir / "config.jsonc";
    configPath.make_preferred();

    if (!ConfigManager::getInstance().load(configPath.string())) {
        log.error("配置文件加载失败：{}", configPath.string());
        return false;
    }

    auto const& cfg = ConfigManager::getInstance().get();
    MobMatcher::getInstance().updateRules(cfg.blocked_mobs, cfg.exempt_mobs, cfg.block_all_non_vanilla_mobs);

    if (cfg.block_all_non_vanilla_mobs) {
        log.info("已开启「一键拦截全部非原版生物」模式");
    }
    log.info(
        "配置加载完成：拦截名单 {} 条，豁免名单 {} 条，配置文件 {}",
        cfg.blocked_mobs.size(),
        cfg.exempt_mobs.size(),
        configPath.string()
    );

    // 说明：此阶段 LeviLamina 尚未 enable 任何 Mod（含 PLand），因此不在 load() 里做
    // PLand 接口自检 —— 放到 enable() 中进行。
    return true;
}

bool Entry::enable() {
    auto& log = getSelf().getLogger();
    log.info("正在启用 TerritoryMobBlocker...");

    auto const& cfg = ConfigManager::getInstance().get();
    log.setLevel(static_cast<ll::io::LogLevel>(cfg.log_level));

    // PLand 作为 manifest 强依赖，在本插件 enable 之前已完成 enable，此处探测才准确。
    auto& bridge = pland::PlandBridge::getInstance();
    if (bridge.probe()) {
        log.info("PLand 接口自检通过（v{}），拦截规则已生效", bridge.version());
    } else {
        log.error("PLand 接口自检失败：本插件暂时不会拦截任何生物。");
        for (auto const& item : bridge.report()) {
            log.error("  [{}] {} {}", item.ok ? "OK" : "FAIL", item.label, item.symbol.empty() ? "" : item.symbol);
        }
        log.error("请确认 PLand 版本为 0.22.x 及以上；运行期会自动重试，也可执行 /tmb probe 手动重探。");
    }

    // 指令注册失败不应连带让插件失效：拦截主体仍可工作，只是管理指令不可用
    try {
        registerCommand();
    } catch (std::exception const& e) {
        log.error("注册 /tmb 指令失败：{}", e.what());
    } catch (...) {
        log.error("注册 /tmb 指令失败：未知异常");
    }

    if (!MobSpawnInterceptor::getInstance().enable()) {
        log.error("注册生物生成拦截监听器失败！");
        return false;
    }

    // 底层实体创建挂钩：补上脚本 dimension.spawnEntity() 这条绕过生成事件的路径
    if (cfg.hook_actor_add) {
        ActorAddHook::getInstance().enable();
    } else {
        log.info("已按配置跳过实体创建挂钩（脚本生成的生物不会被拦截）");
    }

    log.info("TerritoryMobBlocker 已成功启用");
    return true;
}

bool Entry::disable() {
    getSelf().getLogger().info("正在禁用 TerritoryMobBlocker...");
    ActorAddHook::getInstance().disable();
    MobSpawnInterceptor::getInstance().disable();
    return true;
}

bool Entry::unload() {
    getSelf().getLogger().info("正在卸载 TerritoryMobBlocker...");
    return true;
}

} // namespace tmb

LL_REGISTER_MOD(tmb::Entry, tmb::Entry::getInstance());
