#pragma once

#include "Config/Config.h"

#include <string>
#include <vector>

namespace tmb::config_template {

/**
 * 生成带中文注释的 JSONC 配置文本。
 *
 * 与 ConfigManager 分离的原因：这里不依赖日志/Mod 运行时，便于单独做单元测试
 * （渲染结果必须能被 `nlohmann::json::parse(..., ignore_comments=true)` 还原）。
 */
std::string render(Config const& cfg);

/// 渲染 JSON 字符串数组：每项独占一行，空数组渲染为 `[]`
std::string renderStringArray(std::vector<std::string> const& items, std::string const& indent);

} // namespace tmb::config_template
