#include "Config/ConfigTemplate.h"

#include <nlohmann/json.hpp>

#include <sstream>

namespace tmb::config_template {

namespace {

constexpr char const* boolText(bool v) { return v ? "true" : "false"; }

} // namespace

std::string renderStringArray(std::vector<std::string> const& items, std::string const& indent) {
    if (items.empty()) {
        return "[]";
    }

    std::string out = "[\n";
    for (size_t i = 0; i < items.size(); ++i) {
        out += indent;
        out += nlohmann::json(items[i]).dump(); // 借用 nlohmann 完成字符串转义
        if (i + 1 != items.size()) {
            out += ',';
        }
        out += '\n';
    }
    // 收尾方括号比元素少一级缩进
    out += indent.size() >= 4 ? indent.substr(0, indent.size() - 4) : std::string{};
    out += ']';
    return out;
}

std::string render(Config const& cfg) {
    std::string const indent = "        "; // 8 空格，数组元素一级缩进

    std::ostringstream out;
    out << R"JSON({
    // ====================================================================
    // TerritoryMobBlocker 配置文件（JSONC，支持 // 与 /* */ 注释）
    //
    // 作用：当 PLand 领地的「环境权限 -> 怪物生成」为「关闭」时，
    //       拦截本文件名单内、由 Addons 定义的自定义生物在该领地内生成。
    //
    // 修改后可在游戏内执行 /tmb reload 热重载，无需重启服务器。
    // ====================================================================

    // 配置结构版本号，请勿手动修改。
    // 插件升级时若发现此值低于当前版本，会自动迁移并回写（保留你的注释与其它配置）。
    "version": )JSON"
        << cfg.version << R"JSON(,

    // 插件总开关：false = 完全停用拦截逻辑（不影响服务器启动与其它插件）
    "enabled": )JSON"
        << boolText(cfg.enabled) << R"JSON(,

    // --------------------------------------------------------------------
    // 拦截名单
    //
    // 当领地关闭「怪物生成」时，命中此名单的生物禁止在该领地内生成。
    //
    // 支持四种写法（大小写不敏感，会自动剥离 <minecraft:as_adult> 这类事件后缀）：
    //   "my_addon:mutant_creeper"    完整 ID，精确匹配
    //   "mutant_creeper"             简写，匹配任意命名空间下的同名生物
    //   "my_addon:*"                 该命名空间下的全部生物
    //   "boss_*"                     前缀匹配；写成 "ns:boss_*" 则只匹配该命名空间
    //
    // 注意：只有「结尾」的 * 是通配符，出现在中间会被当作普通字符。
    // --------------------------------------------------------------------
    "blocked_mobs": )JSON"
        << renderStringArray(cfg.blocked_mobs, indent) << R"JSON(,

    // --------------------------------------------------------------------
    // 豁免名单
    //
    // 即使命中上面的 blocked_mobs（或已开启下方的「一键非原版拦截」），
    // 命中此名单的生物仍会强制放行。语法与 blocked_mobs 完全一致。
    //
    // 典型用途：把 Addons 里的和平生物（动物 / 宠物 / 坐骑）加进来，
    //           避免开启「一键非原版拦截」后连它们也一起拦掉。
    // --------------------------------------------------------------------
    "exempt_mobs": )JSON"
        << renderStringArray(cfg.exempt_mobs, indent) << R"JSON(,

    // --------------------------------------------------------------------
    // 行为开关
    // --------------------------------------------------------------------

    // 一键拦截全部非原版生物（命名空间不是 minecraft 的生物），免去逐条配置。
    // 与 blocked_mobs 取并集生效；exempt_mobs 优先级最高。
    // 注意：开启后自定义的和平生物也会被拦，请配合 exempt_mobs 使用。
    "block_all_non_vanilla_mobs": )JSON"
        << boolText(cfg.block_all_non_vanilla_mobs) << R"JSON(,

    // 是否拦截自然生成的生物（population 刷怪管线，通常是刷怪量的大头）
    "intercept_natural_spawn": )JSON"
        << boolText(cfg.intercept_natural_spawn) << R"JSON(,

    // 是否拦截非自然生成（刷怪蛋 / 指令 / 刷怪笼 / 组件骑手等）
    "intercept_non_natural_spawn": )JSON"
        << boolText(cfg.intercept_non_natural_spawn) << R"JSON(,

    // --------------------------------------------------------------------
    // 脚本生成路径（v3 新增）
    //
    // 上面的两个生成事件只覆盖「刷怪管线」（自然刷怪 / 刷怪笼 / 指令 / 刷怪蛋）。
    // 而脚本 API `dimension.spawnEntity()` 直接创建实体，完全不经过那条管线 ——
    // 这也是很多 Addon 生物（驯龙高手等）明明在名单里却拦不住的原因。
    // 下面这个开关会让插件挂钩底层「实体创建」，把这类生物一并纳入拦截。
    // --------------------------------------------------------------------

    // 是否挂钩底层实体创建以覆盖脚本生成的生物。关掉后只保留事件通道。
    "hook_actor_add": )JSON"
        << boolText(cfg.hook_actor_add) << R"JSON(,

    // 挂钩通道命中时是否跳过已驯服的生物。
    // 建议保持 true：否则玩家驯养的宠物/坐骑一旦位于禁怪领地内也会被清除。
    "hook_skip_tamed": )JSON"
        << boolText(cfg.hook_skip_tamed) << R"JSON(,

    // 挂钩通道命中后延迟多少 tick 再清除。1 = 下一 tick。
    // 之所以延迟：实体刚被创建时尚未完成注册，立即销毁有重入风险。
    "hook_remove_delay_ticks": )JSON"
        << cfg.hook_remove_delay_ticks << R"JSON(,

    // --------------------------------------------------------------------
    // 新实体监视器（v4 新增）—— 兜底通道，与生成路径无关
    //
    // 引擎创建实体的入口不止一个（Level::$addEntity / $putEntity 的两个重载 /
    // $addGlobalEntity / $addAutonomousEntity），某些脚本 API 走的是别的入口，
    // 表现为"生成成功但插件一条日志都没有"。监视器改为「只认新出现的实体」：
    // 首次扫描登记全部存量为基线，此后从未见过的实体一律按同一套判定处理。
    // --------------------------------------------------------------------

    // 是否启用新实体监视器
    "watch_new_entities": )JSON"
        << boolText(cfg.watch_new_entities) << R"JSON(,

    // 扫描间隔（tick）。4 ≈ 0.2 秒：越小越及时，越大越省性能。
    "watch_interval_ticks": )JSON"
        << cfg.watch_interval_ticks << R"JSON(,

    // 监视器是否跳过已驯服生物（建议 true，避免清掉玩家的宠物）
    "watch_skip_tamed": )JSON"
        << boolText(cfg.watch_skip_tamed) << R"JSON(,

    // Debug 日志是否记录"所有"新实体。默认只记录名单内的，避免刷屏。
    "watch_log_all_new_entities": )JSON"
        << boolText(cfg.watch_log_all_new_entities) << R"JSON(,

    // 是否在控制台打印每一条拦截记录。
    // 默认关闭：领地多、刷怪频繁时逐条打印会刷屏。排查问题时可临时改为 true。
    // 注意：该日志级别为 Info，还需要 log_level >= 3 才会真正输出。
    "log_blocked_spawns": )JSON"
        << boolText(cfg.log_blocked_spawns) << R"JSON(,

    // 日志等级：-1=关闭  0=Fatal  1=Error  2=Warn  3=Info  4=Debug  5=Trace
    // 默认 3(Info)：可看到启动自检结果与配置加载摘要。
    // 若嫌日志多可降到 1（仅错误）；排查接口问题可升到 4 查看 PLand 符号解析细节。
    "log_level": )JSON"
        << cfg.log_level << R"JSON(
}
)JSON";

    return out.str();
}

} // namespace tmb::config_template
