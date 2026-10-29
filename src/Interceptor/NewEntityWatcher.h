#pragma once

#include <memory>

namespace tmb {

/**
 * 新实体监视器（兜底通道）
 *
 * 背景：引擎里"创建实体"的入口不止一个 —— `Level::$addEntity`、
 * `$putEntity`（有 3 参 / 4 参两个重载）、`$addGlobalEntity`、`$addAutonomousEntity`……
 * 逐个挂钩等于打地鼠：某些脚本 API（如 `dimension.spawnEntity()` 的部分路径）走的是
 * 别的入口，表现为「生成成功但插件一条日志都没有」。
 *
 * 本类改为**只认「新出现的实体」**，与创建路径无关：
 *   1. 首次扫描时把当前世界里已有的实体全部记为"存量"，永不处理；
 *   2. 之后每隔 `watch_interval_ticks` 扫描一次，凡是不在名单里出现过的实体即视为"新生成"，
 *      按与其它通道完全相同的判定处理，命中则 despawn；
 *   3. 每轮用当前实体列表重建集合，内存不会无限增长。
 *
 * 这样既能覆盖任意创建路径，又严格满足「只拦生成、不处理存量」。
 * 唯一代价是清理延迟：最多 `watch_interval_ticks` tick（默认 4 tick ≈ 0.2 秒）。
 */
class NewEntityWatcher {
public:
    static NewEntityWatcher& getInstance();

    NewEntityWatcher();
    ~NewEntityWatcher();

    bool enable();
    bool disable();

    [[nodiscard]] bool isEnabled() const;

    /// 由 tick 回调驱动（内部使用）
    void onLevelTick(void* levelPtr);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace tmb
