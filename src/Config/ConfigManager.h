#pragma once

#include "Config/Config.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tmb {

/**
 * 配置管理器（JSONC）
 *
 * - 配置文件为 `config/config.jsonc`，允许行注释与块注释；
 * - 首次运行会写出一份带完整中文注释的默认配置；
 * - 从旧的 `config.json` 自动迁移（迁移后把旧文件重命名为 `config.json.bak`）；
 * - 加载成功后**不再回写**，以保留用户的注释与排版。
 */
class ConfigManager {
public:
    static ConfigManager& getInstance();

    ConfigManager();
    ~ConfigManager();

    /// 加载配置；文件不存在时生成默认配置
    bool load(const std::string& path);

    /// 按当前内存中的配置生成带注释的 JSONC 文件（会覆盖原文件）
    bool save();

    /// 热重载（沿用首次加载的路径）
    bool reload();

    Config&            get();
    const Config&      get() const;
    const std::string& getPath() const;

private:
    /// 读取单个已存在的配置文件（含自动版本升级）
    bool loadFile(std::filesystem::path const& path);

    /// 纯解析（不做版本升级），供旧格式迁移使用
    bool readAndParse(std::filesystem::path const& path);

    /**
     * 版本号低于当前版本时自动升级：
     *   1. 应用版本迁移（只改「沿用旧默认值」的字段）；
     *   2. 优先在原文上就地替换标量值，保留用户注释与排版；
     *   3. 就地替换不可行（缺字段/结构变化/校验不通过）时，备份后整文件重写。
     */
    bool upgradeIfNeeded(
        std::filesystem::path const& path,
        nlohmann::json const&        parsed,
        std::string const&           raw
    );

    /// 按配置里的 log_level 应用日志等级
    void applyLogLevel();

    std::string             mConfigPath;
    std::unique_ptr<Config> mConfig;
};

} // namespace tmb
