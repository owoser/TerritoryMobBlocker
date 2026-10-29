#include "Interceptor/MobMatcher.h"

#include <cctype>

namespace tmb {

MobMatcher& MobMatcher::getInstance() {
    static MobMatcher instance;
    return instance;
}

std::string MobMatcher::normalizeIdentifier(std::string_view rawId) {
    // 1. 去掉事件后缀："custom:mob<minecraft:as_adult>" -> "custom:mob"
    if (auto anglePos = rawId.find('<'); anglePos != std::string_view::npos) {
        rawId = rawId.substr(0, anglePos);
    }

    // 2. 去首尾空白
    while (!rawId.empty() && std::isspace(static_cast<unsigned char>(rawId.front()))) {
        rawId.remove_prefix(1);
    }
    while (!rawId.empty() && std::isspace(static_cast<unsigned char>(rawId.back()))) {
        rawId.remove_suffix(1);
    }

    // 3. 转小写
    std::string result;
    result.reserve(rawId.size());
    for (char ch : rawId) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

bool MobMatcher::isVanilla(std::string_view normalizedId) { return normalizedId.starts_with("minecraft:"); }

std::vector<MatchRule> MobMatcher::compile(std::vector<std::string> const& patterns) {
    std::vector<MatchRule> rules;
    rules.reserve(patterns.size());

    for (auto const& raw : patterns) {
        std::string pat = normalizeIdentifier(raw);
        if (pat.empty()) {
            continue;
        }

        MatchRule rule;
        rule.pattern = pat;

        if (pat == "*") {
            rule.type = MatchType::All;
        } else if (pat.ends_with(":*")) {
            rule.type = MatchType::Namespace;
            rule.ns   = pat.substr(0, pat.size() - 2);
        } else if (pat.ends_with('*')) {
            rule.type   = MatchType::Prefix;
            rule.prefix = pat.substr(0, pat.size() - 1);
        } else {
            rule.type = MatchType::Exact;
        }

        rules.push_back(std::move(rule));
    }

    return rules;
}

void MobMatcher::updateRules(
    std::vector<std::string> const& blockPatterns,
    std::vector<std::string> const& exemptPatterns,
    bool                            blockAllNonVanilla
) {
    mBlockAllNonVanilla = blockAllNonVanilla;
    mBlockRules         = compile(blockPatterns);
    mExemptRules        = compile(exemptPatterns);
}

bool MobMatcher::matchesAny(std::string_view cleanId, std::vector<MatchRule> const& rules) {
    if (cleanId.empty()) {
        return false;
    }

    bool         hasColon = cleanId.find(':') != std::string_view::npos;
    std::string_view shortName = cleanId;
    if (hasColon) {
        shortName = cleanId.substr(cleanId.find(':') + 1);
    }

    for (auto const& rule : rules) {
        switch (rule.type) {
        case MatchType::All:
            return true;

        case MatchType::Exact:
            // 完整 ID 精确匹配；无命名空间的简写也可匹配同名生物
            if (cleanId == rule.pattern) {
                return true;
            }
            if (hasColon && shortName == rule.pattern) {
                return true;
            }
            break;

        case MatchType::Namespace: {
            std::string_view ns = rule.ns;
            if (cleanId.starts_with(ns) && cleanId.size() > ns.size() && cleanId[ns.size()] == ':') {
                return true;
            }
            break;
        }

        case MatchType::Prefix:
            // 无命名空间的写法（如 "boss_*"）对任意命名空间生效；
            // 带命名空间的写法（如 "my_addon:boss_*"）只匹配该命名空间。
            if (cleanId.starts_with(rule.prefix)) {
                return true;
            }
            if (hasColon && shortName.starts_with(rule.prefix)) {
                return true;
            }
            break;
        }
    }

    return false;
}

bool MobMatcher::matchesBlockList(std::string_view rawId) const {
    std::string cleanId = normalizeIdentifier(rawId);
    if (cleanId.empty()) {
        return false;
    }

    if (mBlockAllNonVanilla && !isVanilla(cleanId)) {
        return true;
    }

    return matchesAny(cleanId, mBlockRules);
}

bool MobMatcher::isExempt(std::string_view rawId) const {
    std::string cleanId = normalizeIdentifier(rawId);
    if (cleanId.empty() || mExemptRules.empty()) {
        return false;
    }
    return matchesAny(cleanId, mExemptRules);
}

bool MobMatcher::isBlocked(std::string_view rawId) const { return matchesBlockList(rawId) && !isExempt(rawId); }

} // namespace tmb
