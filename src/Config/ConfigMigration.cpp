#include "Config/ConfigMigration.h"

#include <regex>

namespace tmb::config_migration {

namespace {

constexpr char const* boolLiteral(bool v) { return v ? "true" : "false"; }

} // namespace

std::vector<FieldOverride> apply(Config& cfg, int from, int to) {
    std::vector<FieldOverride> overrides;

    // ---- v1 -> v2 ------------------------------------------------------
    // v2 起：逐条拦截日志默认关闭（v1 的默认值是 true，领地多时会刷屏）
    if (from < 2 && to >= 2) {
        if (cfg.log_blocked_spawns) {
            cfg.log_blocked_spawns = false;
            overrides.push_back(
                {"log_blocked_spawns",
                 boolLiteral(false),
                 "log_blocked_spawns: true -> false（v2 起默认关闭，避免逐条拦截日志刷屏）"}
            );
        }
        // v2 起：日志等级回到 Info（v1 中途有过 1=仅错误 的默认值，会让启动自检不可见）
        if (cfg.log_level == 1) {
            cfg.log_level = 3;
            overrides.push_back({"log_level", "3", "log_level: 1 -> 3（v2 起默认 Info，启动自检可见）"});
        }
    }

    // ---- v3 -> v4 ------------------------------------------------------
    // v4 新增「新实体监视器」四个字段（纯新增，值沿用默认即可，无需回写）。
    if (from < 4 && to >= 4) {
        // 缺字段时由调用方走「备份 + 整文件重写」补齐
    }

    // ---- v2 -> v3 ------------------------------------------------------
    // v3 新增三个「底层实体创建挂钩」配置项。纯新增字段，值本身沿用默认即可，
    // 因此这里不产生字段覆盖；文件缺字段时由调用方走「备份 + 整文件重写」路径补齐。
    // （保留分支是为了让迁移链条完整、后续需要时可在此追加值修正）
    if (from < 3 && to >= 3) {
        // 钩子/延迟参数在 v3 之前不存在，无需回写任何字段
    }

    return overrides;
}

bool replaceScalarInPlace(std::string& text, std::string_view key, std::string const& literal) {
    std::string const pattern =
        "(\"" + std::string(key) + "\"\\s*:\\s*)(\"(?:[^\"\\\\]|\\\\.)*\"|true|false|-?[0-9]+(?:\\.[0-9]+)?)";

    std::regex  re(pattern);
    std::smatch m;
    if (!std::regex_search(text, m, re)) {
        return false;
    }

    text = m.prefix().str() + m[1].str() + literal + m.suffix().str();
    return true;
}

bool hasAllFields(nlohmann::json const& parsed) {
    auto const schema = nlohmann::json(Config{});
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        if (!parsed.contains(it.key())) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> tryUpgradeInPlace(
    std::string const&                raw,
    nlohmann::json const&             parsed,
    Config const&                     merged,
    std::vector<FieldOverride> const& overrides
) {
    if (raw.empty() || !hasAllFields(parsed)) {
        return std::nullopt;
    }

    std::string updated = raw;

    for (auto const& ov : overrides) {
        if (!replaceScalarInPlace(updated, ov.key, ov.literal)) {
            return std::nullopt;
        }
    }
    if (!replaceScalarInPlace(updated, "version", std::to_string(merged.version))) {
        return std::nullopt;
    }

    // 安全阀：改写后的文本必须仍能解析，且语义与内存配置完全一致
    try {
        auto const reparsed = nlohmann::json::parse(updated, nullptr, true, true);
        if (nlohmann::json(reparsed.get<Config>()) != nlohmann::json(merged)) {
            return std::nullopt;
        }
    } catch (nlohmann::json::exception const&) {
        return std::nullopt;
    }

    return updated;
}

} // namespace tmb::config_migration
