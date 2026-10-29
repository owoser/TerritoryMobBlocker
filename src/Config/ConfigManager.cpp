#include "Config/ConfigManager.h"

#include "Config/ConfigMigration.h"
#include "Config/ConfigTemplate.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace tmb {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kLegacyFileName = "config.json";

/// 读取文件全部内容
bool readFile(fs::path const& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

/// 写入文件（整体覆盖）
bool writeFile(fs::path const& path, std::string_view content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    return file.good();
}

} // namespace

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::ConfigManager() : mConfig(std::make_unique<Config>()) {}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::load(const std::string& path) {
    mConfigPath = path;
    fs::path const target(path);
    fs::path const dir = target.parent_path();

    std::error_code ec;
    if (!dir.empty() && !fs::exists(dir, ec)) {
        fs::create_directories(dir, ec);
        if (ec) {
            logger.error("创建配置目录失败：{}（{}）", dir.string(), ec.message());
        }
    }

    // 1. 目标 JSONC 已存在 -> 读取（并按需自动升级版本）
    if (fs::exists(target, ec)) {
        if (!loadFile(target)) {
            return false;
        }
        applyLogLevel();
        return true;
    }

    // 2. 存在旧版 config.json -> 直接读入，再由 save() 写成带注释的新格式
    fs::path const legacy = dir / std::string{kLegacyFileName};
    if (fs::exists(legacy, ec)) {
        logger.info("检测到旧版配置文件 {}，正在迁移为 {}", legacy.filename().string(), target.filename().string());
        bool ok = readAndParse(legacy);
        if (!ok) {
            logger.warn("旧版配置文件解析失败，将改写为新的默认配置");
            *mConfig = Config{};
        }
        mConfig->version = kCurrentConfigVersion;
        if (!save()) {
            return false;
        }
        if (ok) {
            fs::path const backup = fs::path(legacy.string() + ".bak");
            fs::remove(backup, ec);
            fs::rename(legacy, backup, ec);
            if (ec) {
                logger.warn("旧版配置文件备份失败：{}", ec.message());
            } else {
                logger.info("配置迁移完成，旧文件已备份为 {}", backup.filename().string());
            }
        }
        applyLogLevel();
        return true;
    }

    // 3. 全新安装 -> 写出带注释的默认配置
    logger.info("未找到配置文件，正在生成带注释的默认配置：{}", path);
    if (!save()) {
        return false;
    }
    applyLogLevel();
    return true;
}

bool ConfigManager::loadFile(fs::path const& path) {
    std::string raw;
    if (!readFile(path, raw)) {
        logger.error("无法打开配置文件：{}", path.string());
        return false;
    }

    nlohmann::json parsed;
    try {
        // 第三个参数 ignore_comments=true：支持 // 与 /* */ 注释
        // 第四个参数 ignore_trailing_commas=true：容忍末尾多余的逗号
        parsed = nlohmann::json::parse(raw, nullptr, true, true);
    } catch (nlohmann::json::exception const& e) {
        logger.error("解析配置文件失败：{}。请检查 JSONC 语法是否有误（多余逗号、缺少引号等）！", e.what());
        return false;
    }

    try {
        // 缺失字段自动取结构体默认值
        *mConfig = parsed.get<Config>();
    } catch (nlohmann::json::exception const& e) {
        logger.error("配置文件字段类型不匹配：{}", e.what());
        return false;
    }

    return upgradeIfNeeded(path, parsed, raw);
}

bool ConfigManager::upgradeIfNeeded(fs::path const& path, nlohmann::json const& parsed, std::string const& raw) {
    int const from = parsed.value("version", 1);

    if (from > kCurrentConfigVersion) {
        logger.warn(
            "配置文件版本 ({}) 高于本插件支持的版本 ({})，可能来自更新的插件；将按兼容模式读取，建议备份后重新生成。",
            from,
            kCurrentConfigVersion
        );
        return true;
    }
    if (from == kCurrentConfigVersion) {
        return true; // 版本一致：不做任何写入，保留用户注释
    }

    auto overrides   = config_migration::apply(*mConfig, from, kCurrentConfigVersion);
    mConfig->version = kCurrentConfigVersion;

    // --- 先尝试「就地改值」，保留用户注释与排版 ---------------------------
    if (auto upgraded = config_migration::tryUpgradeInPlace(raw, parsed, *mConfig, overrides)) {
        if (writeFile(path, *upgraded)) {
            logger.info("配置文件已从 v{} 自动升级到 v{}（保留原有注释与排版）", from, kCurrentConfigVersion);
            for (auto const& ov : overrides) {
                logger.info("  · {}", ov.note);
            }
            return true;
        }
    }

    // --- 回退：整文件重写（先备份） ---------------------------------------
    logger.warn(
        "无法在原文上就地升级配置（缺少字段或结构变化），将按当前模板整体重写；原文件备份为 {}.bak",
        path.filename().string()
    );

    std::error_code ec;
    fs::path const  backup = fs::path(path.string() + ".bak");
    fs::remove(backup, ec);
    fs::copy_file(path, backup, ec);
    if (ec) {
        logger.warn("备份配置文件失败：{}", ec.message());
    }

    if (!save()) {
        return false;
    }
    logger.info("配置文件已从 v{} 升级到 v{}{}", from, kCurrentConfigVersion, overrides.empty() ? "" : "，变更：");
    for (auto const& ov : overrides) {
        logger.info("  · {}", ov.note);
    }
    return true;
}

bool ConfigManager::readAndParse(fs::path const& path) {
    std::string content;
    if (!readFile(path, content)) {
        logger.error("无法打开配置文件：{}", path.string());
        return false;
    }

    try {
        auto j   = nlohmann::json::parse(content, nullptr, true, true);
        *mConfig = j.get<Config>();
    } catch (nlohmann::json::exception const& e) {
        logger.error("解析配置文件失败：{}。请检查 JSONC 语法是否有误（多余逗号、缺少引号等）！", e.what());
        return false;
    }

    return true;
}

void ConfigManager::applyLogLevel() {
    logger.setLevel(static_cast<ll::io::LogLevel>(mConfig->log_level));
}

bool ConfigManager::save() {
    try {
        fs::path const p(mConfigPath);
        if (p.has_parent_path() && !fs::exists(p.parent_path())) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }

        if (!writeFile(p, config_template::render(*mConfig))) {
            logger.error("写入配置文件失败：{}", mConfigPath);
            return false;
        }
    } catch (std::exception const& e) {
        logger.error("保存配置文件发生异常：{}", e.what());
        return false;
    }
    return true;
}

bool ConfigManager::reload() {
    if (mConfigPath.empty()) {
        logger.warn("配置文件路径尚未初始化，无法重载！");
        return false;
    }
    return load(mConfigPath);
}

Config&            ConfigManager::get() { return *mConfig; }
const Config&      ConfigManager::get() const { return *mConfig; }
const std::string& ConfigManager::getPath() const { return mConfigPath; }

} // namespace tmb
