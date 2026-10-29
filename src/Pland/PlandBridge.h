#pragma once

/**
 * PLand 运行期桥接层
 *
 * 设计要点：
 *   PLand 是运行期插件（plugins/PLand/PLand.dll），本插件不在链接期引用它的导入库，
 *   而是启动时通过 LL 的 ModManagerRegistry 取得模块句柄，再用 GetProcAddress 逐个解析
 *   所需的导出符号。
 *
 *   好处：PLand 升级后即使某个符号改名/缺失，本插件 DLL 仍能正常加载，只是对应功能
 *   安全降级并给出明确告警，而不会出现「导入表缺失 -> 整个插件加载失败」。
 *
 * 已实测的导出符号（PLand 0.22.2 与 0.25.0 完全一致）：
 *   ?getInstance@PLand@land@@SAAEAV12@XZ
 *   ?getLandRegistry@PLand@land@@QEBAAEAVLandRegistry@2@XZ
 *   ?getLandAt@LandRegistry@land@@QEBA?AV?$shared_ptr@VLand@land@@@std@@AEBVBlockPos@@H@Z
 *   ?getPermTable@Land@land@@QEBAAEBULandPermTable@2@XZ
 *   ?getId@Land@land@@QEBA_JXZ
 *   ?getName@Land@land@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ
 */

#include "mc/world/level/BlockPos.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tmb::pland {

/// 单个导出符号的解析结果（用于启动自检报告）
struct ProbeItem {
    std::string label;
    std::string symbol;
    bool        ok{false};
    void*       address{nullptr};
};

/// 领地信息快照
struct LandInfo {
    bool        exists{false};
    int64_t     id{-1};
    std::string name;
    bool        allowMonsterSpawn{true};
    bool        allowAnimalSpawn{true};
};

class PlandBridge {
public:
    static PlandBridge& getInstance();

    /// 解析并校验 PLand 导出符号；返回是否可用（可重复调用以实现热重连）
    bool probe();

    /**
     * 确保接口可用；不可用时按冷却时间自动重探。
     *
     * 为什么需要它：LeviLamina 是「先加载全部 Mod，再逐个 enable」，因此在其它 Mod 的
     * load() 阶段 PLand 尚未 Enabled。另外若 PLand 被热重载，句柄也会失效。
     * 这里做带冷却的重探，既能覆盖加载时序，又不会在热路径上反复解析符号。
     */
    /**
     * 确保接口可用；不可用时按冷却时间自动重探。
     *
     * 为什么需要它：LeviLamina 是「先加载全部 Mod，再逐个 enable」，因此在其它 Mod 的
     * load() 阶段 PLand 尚未 Enabled。另外若 PLand 被热重载，句柄也会失效。
     * 这里做带冷却的重探，既能覆盖加载时序，又不会在热路径上反复解析符号。
     */
    bool ensureAvailable();
    [[nodiscard]] bool                          isAvailable() const { return mAvailable; }
    [[nodiscard]] std::string const&            version() const { return mVersion; }
    [[nodiscard]] std::vector<ProbeItem> const& report() const { return mReport; }

    /// 权限字段布局是否已通过实测校验（未校验或校验失败时为 false）
    [[nodiscard]] bool isPermsLayoutValid() const { return mPermsLayoutValid; }

    /// 查询方块坐标所在领地的环境权限（pos 为方块坐标，dimId 为维度 ID）
    [[nodiscard]] std::optional<LandInfo> landAt(BlockPos const& pos, int dimId) const;

private:
    PlandBridge() = default;

    // 注意：MSVC x64 下返回非平凡类型（如 std::shared_ptr）的成员函数，
    // 第一个参数是 this，第二个参数是返回值缓冲区指针（已在本机工具链实测确认）。
    using GetInstanceFn = void* (*)();
    using GetRegistryFn = void* (*)(void const*);
    using GetLandAtFn   = void (*)(void const* /*this*/, std::shared_ptr<void>* /*ret*/, BlockPos const&, int);
    using GetIdFn       = int64_t (*)(void const*);
    using GetNameFn     = std::string const* (*)(void const*);
    using GetPermTableFn = void const* (*)(void const*);

    bool                   mAvailable{false};
    void*                  mHandle{nullptr};
    std::string            mVersion;
    std::vector<ProbeItem> mReport;

    /// 权限布局可信度：一次成功读取后即固定；发现异常值时永久降级为"不拦截"
    mutable bool mPermsLayoutChecked{false};
    mutable bool mPermsLayoutValid{true};

    /// 上次 probe 的时间点（用于 ensureAvailable 的冷却控制）
    std::chrono::steady_clock::time_point mLastProbe{};

    GetInstanceFn  mGetInstance{nullptr};
    GetRegistryFn  mGetRegistry{nullptr};
    GetLandAtFn    mGetLandAt{nullptr};
    GetIdFn        mGetId{nullptr};
    GetNameFn      mGetName{nullptr};
    GetPermTableFn mGetPermTable{nullptr};
};

} // namespace tmb::pland
