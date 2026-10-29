#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tmb {

enum class MatchType {
    Exact,     // 精确匹配，例如 "my_addon:mutant_creeper"
    Namespace, // 命名空间通配，例如 "my_addon:*"
    Prefix,    // 前缀匹配，例如 "my_addon:mutant_*"
    All,       // 通配所有，"*"
};

struct MatchRule {
    MatchType   type{MatchType::Exact};
    std::string pattern; // 规范化后的原始模式（用于精确匹配）
    std::string ns;      // 命名空间（Namespace 规则）
    std::string prefix;  // 前缀（Prefix 规则）
};

/**
 * 生物 ID 匹配器
 *
 * 支持四种写法（大小写不敏感，自动去除 `<事件名>` 后缀与首尾空白）：
 *   1. 完整 ID      ：my_addon:mutant_creeper
 *   2. 简写 ID      ：mutant_creeper      —— 匹配任意命名空间下的同名生物
 *   3. 命名空间通配 ：my_addon:*          —— 匹配该命名空间下全部生物
 *   4. 前缀通配     ：my_addon:mutant_*   —— 匹配以该前缀开头的生物
 *   另可开启"一键拦截全部非原版生物"开关，此时任何非 minecraft: 的生物都会命中。
 */
class MobMatcher {
public:
    static MobMatcher& getInstance();

    /// 重新编译匹配规则
    void updateRules(
        std::vector<std::string> const& blockPatterns,
        std::vector<std::string> const& exemptPatterns,
        bool                            blockAllNonVanilla
    );

    /// 规范化实体 ID：去掉 `<event>` 后缀、去首尾空白、转小写
    static std::string normalizeIdentifier(std::string_view rawId);

    /// 是否为原版生物（命名空间为 minecraft）
    static bool isVanilla(std::string_view normalizedId);

    /// 是否命中黑名单（不考虑豁免）
    [[nodiscard]] bool matchesBlockList(std::string_view rawId) const;

    /// 是否命中豁免名单
    [[nodiscard]] bool isExempt(std::string_view rawId) const;

    /// 最终拦截判定：命中黑名单且未命中豁免名单
    [[nodiscard]] bool isBlocked(std::string_view rawId) const;

    [[nodiscard]] size_t blockRuleCount() const { return mBlockRules.size(); }
    [[nodiscard]] size_t exemptRuleCount() const { return mExemptRules.size(); }
    [[nodiscard]] bool   isBlockAllNonVanillaEnabled() const { return mBlockAllNonVanilla; }

private:
    MobMatcher() = default;

    static std::vector<MatchRule> compile(std::vector<std::string> const& patterns);
    static bool                   matchesAny(std::string_view cleanId, std::vector<MatchRule> const& rules);

    std::vector<MatchRule> mBlockRules;
    std::vector<MatchRule> mExemptRules;
    bool                   mBlockAllNonVanilla{false};
};

} // namespace tmb
