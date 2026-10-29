#include "Pland/PlandBridge.h"

#include "logger.h"

#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"

#include <Windows.h>

#include <cstddef>

namespace tmb::pland {

namespace {

/// PLand 导出符号（MSVC x64 修饰名），已对 0.22.2 / 0.25.0 实测一致
constexpr char const* kGetInstance  = "?getInstance@PLand@land@@SAAEAV12@XZ";
constexpr char const* kGetRegistry  = "?getLandRegistry@PLand@land@@QEBAAEAVLandRegistry@2@XZ";
constexpr char const* kGetLandAt    = "?getLandAt@LandRegistry@land@@QEBA?AV?$shared_ptr@VLand@land@@@std@@"
                                      "AEBVBlockPos@@H@Z";
constexpr char const* kGetId        = "?getId@Land@land@@QEBA_JXZ";
constexpr char const* kGetName      = "?getName@Land@land@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@"
                                      "V?$allocator@D@2@@std@@XZ";
constexpr char const* kGetPermTable = "?getPermTable@Land@land@@QEBAAEBULandPermTable@2@XZ";

/**
 * PLand 结构体只读镜像（仅声明本插件需要读取的前缀字段）
 *
 * 依据 PLand 官方头文件 land/land/repo/LandContext.h：
 *   struct LandPermTable { EnvironmentPerms environment; RolePerms role; };
 *   struct EnvironmentPerms {
 *       bool allowFireSpread;   // 第 1 个字段
 *       bool allowMonsterSpawn; // 第 2 个字段
 *       bool allowAnimalSpawn;  // 第 3 个字段
 *       ...
 *   };
 *
 * PLand 官方明确约定「新增权限保持 ABI 稳定，不在前面插入字段，只在末尾追加」，
 * 因此这三个 bool 的相对偏移恒定。本插件不构造该类型，只按指针读取字段。
 */
struct EnvironmentPermsPrefix {
    bool allowFireSpread{true};
    bool allowMonsterSpawn{true};
    bool allowAnimalSpawn{true};
};

struct LandPermTablePrefix {
    EnvironmentPermsPrefix environment{};
};

static_assert(offsetof(EnvironmentPermsPrefix, allowMonsterSpawn) == 1, "allowMonsterSpawn 必须位于偏移 1");
static_assert(offsetof(EnvironmentPermsPrefix, allowAnimalSpawn) == 2, "allowAnimalSpawn 必须位于偏移 2");

} // namespace

PlandBridge& PlandBridge::getInstance() {
    static PlandBridge instance;
    return instance;
}

bool PlandBridge::probe() {
    mAvailable   = false;
    mHandle      = nullptr;
    mVersion.clear();
    mReport.clear();
    mPermsLayoutChecked = false;
    mPermsLayoutValid   = true;
    mLastProbe          = std::chrono::steady_clock::now();
    mGetInstance  = nullptr;
    mGetRegistry  = nullptr;
    mGetLandAt    = nullptr;
    mGetId        = nullptr;
    mGetName      = nullptr;
    mGetPermTable = nullptr;

    auto& registry = ll::mod::ModManagerRegistry::getInstance();
    auto  mod      = registry.getMod("PLand");
    if (!mod) {
        mReport.push_back({"PLand 已安装", "", false, nullptr});
        return false;
    }
    // 注意：LeviLamina 是「先加载全部 Mod，再逐个 enable」，因此在 load() 阶段
    // PLand 的状态还不是 Enabled。这里只在「被显式禁用」时拒绝，
    // 真正的可用性由导出符号是否齐全来判定；enable() 阶段会再探测一次。
    if (mod->isDisabled()) {
        mReport.push_back({"PLand 未被禁用", "", false, nullptr});
        return false;
    }
    if (registry.getModType("PLand") != ll::mod::NativeModManagerName) {
        mReport.push_back({"PLand 为原生插件", "", false, nullptr});
        return false;
    }

    auto nativeMod = std::static_pointer_cast<ll::mod::NativeMod>(mod);
    mHandle        = static_cast<void*>(nativeMod->getHandle());
    if (!mHandle) {
        mReport.push_back({"PLand 模块句柄", "", false, nullptr});
        return false;
    }

    if (auto const& ver = mod->getManifest().version; ver) {
        mVersion = ver->to_string();
    } else {
        mVersion = "unknown";
    }
    mReport.push_back({"PLand 模块已加载 v" + mVersion, "", true, mHandle});

    auto resolve = [this](char const* label, char const* symbol) -> void* {
        auto* addr = reinterpret_cast<void*>(
            ::GetProcAddress(static_cast<HMODULE>(mHandle), symbol)
        );
        mReport.push_back({label, symbol, addr != nullptr, addr});
        return addr;
    };

    mGetInstance  = reinterpret_cast<GetInstanceFn>(resolve("PLand::getInstance()", kGetInstance));
    mGetRegistry  = reinterpret_cast<GetRegistryFn>(resolve("PLand::getLandRegistry()", kGetRegistry));
    mGetLandAt    = reinterpret_cast<GetLandAtFn>(resolve("LandRegistry::getLandAt(BlockPos, dimId)", kGetLandAt));
    mGetId        = reinterpret_cast<GetIdFn>(resolve("Land::getId()", kGetId));
    mGetName      = reinterpret_cast<GetNameFn>(resolve("Land::getName()", kGetName));
    mGetPermTable = reinterpret_cast<GetPermTableFn>(resolve("Land::getPermTable()", kGetPermTable));

    for (auto const& item : mReport) {
        if (!item.ok) {
            mAvailable = false;
            return false;
        }
    }

    mAvailable = true;
    logger.debug("PLand 接口解析成功（v{}），共校验 {} 个导出符号", mVersion, mReport.size());
    return true;
}

std::optional<LandInfo> PlandBridge::landAt(BlockPos const& pos, int dimId) const {
    if (!mAvailable || !mGetInstance || !mGetRegistry || !mGetLandAt) {
        return std::nullopt;
    }

    void const* pland = mGetInstance();
    if (!pland) {
        return std::nullopt;
    }
    void const* landRegistry = mGetRegistry(pland);
    if (!landRegistry) {
        return std::nullopt;
    }

    std::shared_ptr<void> land;
    mGetLandAt(landRegistry, &land, pos, dimId);
    if (!land) {
        return std::nullopt;
    }

    LandInfo info;
    info.exists = true;
    info.id     = mGetId ? mGetId(land.get()) : -1;
    if (mGetName) {
        if (auto* name = mGetName(land.get()); name) {
            info.name = *name;
        }
    }
    if (mGetPermTable) {
        if (auto* perms = mGetPermTable(land.get()); perms) {
            auto const* table = reinterpret_cast<LandPermTablePrefix const*>(perms);

            // 布局校验（仅首次读取时执行一次）：
            // 这三个字段都是 bool，正常取值只能是 0/1；若读到其它字节，说明 PLand
            // 的 EnvironmentPerms 前缀布局与预期不符（版本不兼容），此时降级为"不拦截"，
            // 避免在允许生成怪物的领地里误杀实体。
            if (!mPermsLayoutChecked) {
                mPermsLayoutChecked          = true;
                auto const* raw              = reinterpret_cast<unsigned char const*>(table);
                mPermsLayoutValid = raw[0] <= 1 && raw[1] <= 1 && raw[2] <= 1;
                if (!mPermsLayoutValid) {
                    logger.error(
                        "PLand 环境权限字段布局校验失败 (bytes={}, {}, {})，"
                        "可能是 PLand 版本不兼容；为避免误判，本插件将不做任何拦截。",
                        static_cast<int>(raw[0]),
                        static_cast<int>(raw[1]),
                        static_cast<int>(raw[2])
                    );
                }
            }

            if (mPermsLayoutValid) {
                info.allowMonsterSpawn = table->environment.allowMonsterSpawn;
                info.allowAnimalSpawn  = table->environment.allowAnimalSpawn;
            }
        }
    }

    return info;
}

bool PlandBridge::ensureAvailable() {
    if (mAvailable) {
        return true;
    }

    // 冷却：避免热路径上反复枚举模块/解析符号
    static constexpr auto kRetryCooldown = std::chrono::seconds(5);
    auto const            now            = std::chrono::steady_clock::now();
    if (mLastProbe.time_since_epoch().count() != 0 && now - mLastProbe < kRetryCooldown) {
        return false;
    }

    if (probe()) {
        logger.info("PLand 接口已恢复可用（v{}），开始生效拦截规则", mVersion);
        return true;
    }
    return false;
}

} // namespace tmb::pland
