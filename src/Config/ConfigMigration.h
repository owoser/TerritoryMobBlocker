#pragma once

#include "Config/Config.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tmb::config_migration {

/// 某次迁移需要对配置文件做的单字段覆盖（用于在原文上就地替换，保留用户注释）
struct FieldOverride {
    std::string key;
    std::string literal; // 已序列化好的 JSON 字面量
    std::string note;    // 写进日志的说明
};

/**
 * 配置版本迁移
 *
 * 把 `from` -> `to` 之间的语义变更应用到 cfg 上，并把需要回写文件的字段记进返回值。
 *
 * 原则：只调整「沿用旧默认值」的字段，用户显式改过的其它值一律不动。
 */
std::vector<FieldOverride> apply(Config& cfg, int from, int to);

/**
 * 在原始文本里就地替换某个「标量字段」的值（只替换第一处匹配）。
 *
 * 相比整文件重写，这样做能完整保留用户自己写的注释与排版。
 * 只支持字符串 / 布尔 / 数字三种标量；遇到对象或数组不会匹配，由调用方回退到整文件重写。
 */
bool replaceScalarInPlace(std::string& text, std::string_view key, std::string const& literal);

/// 文件是否包含 Config 的全部字段（缺字段就无法只靠替换完成升级）
bool hasAllFields(nlohmann::json const& parsed);

/**
 * 尝试「就地升级」配置文本。
 *
 * 返回 std::nullopt 表示必须回退到整文件重写，原因包括：缺字段、某个字段无法定位、
 * 或改写后的文本重新解析出来与预期语义不一致（最后这道校验是安全阀）。
 */
std::optional<std::string> tryUpgradeInPlace(
    std::string const&                raw,
    nlohmann::json const&             parsed,
    Config const&                     merged,
    std::vector<FieldOverride> const& overrides
);

} // namespace tmb::config_migration
