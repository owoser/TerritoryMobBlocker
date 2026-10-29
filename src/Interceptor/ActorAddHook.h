#pragma once

#include <memory>

namespace tmb {

/**
 * 底层「实体创建」挂钩（补脚本生成路径）
 *
 * 背景：LeviLamina 的 SpawningMobEvent / SpawnedMobEvent 只挂在
 * `BedrockSpawner::$spawnMob` 上，覆盖自然刷怪、刷怪笼、`/summon`、刷怪蛋。
 * 而脚本 API `dimension.spawnEntity()` 直接构造并加入实体，完全绕过该函数 ——
 * 这是本插件原本漏拦 Addon 生物的根因。
 *
 * 这里挂在所有创建路径的公共汇合点 `Level::$addEntity` 上：先让引擎完成创建，
 * 拿到 `Actor*` 后再判定；命中则把实体 ID 入队，于下一 tick 安全清除
 * （addEntity 尚未返回时立即销毁有重入风险）。
 *
 * 挂钩失败（符号缺失 / 版本不匹配）时只告警并降级，不影响其它通道。
 */
class ActorAddHook {
public:
    static ActorAddHook& getInstance();

    ActorAddHook();
    ~ActorAddHook();

    /// 安装挂钩与清理定时器
    bool enable();

    /// 卸载挂钩并清空待清理队列
    bool disable();

    [[nodiscard]] bool isEnabled() const;

    /// 由挂钩回调：实体刚被创建（内部使用）
    void onActorCreated(void* actorPtr);

    /// 由 tick 回调：处理待清理队列（内部使用）
    void onLevelTick(void* levelPtr);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace tmb
