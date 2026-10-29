# 更新日志

## 1.1.1

### 新增

- **`/tmb sweep [半径] [跳过驯服]`：手动清理存量**
  本插件默认**只拦生成、不清扫存量**（避免误删玩家已驯养/摆放好的生物）。
  这个命令用于清理"之前已经刷出来的"生物：以玩家为中心扫描周围实体，
  把「命中名单 + 所在领地禁止怪物生成」的实体清除，并输出扫描/命中/清除/跳过驯服的数量。
  仅全局 OP 可用，且只在你主动执行时生效。

### 改进

- **诊断日志补齐关键信息**：`[ActorAdd]` 现在带坐标、维度，以及 `[名单内]` `[已驯服]` 标记；
  被 `hook_skip_tamed` 跳过的生物会额外打印一行原因。
  之前只有实体 ID，导致「名单里明明有、控制台却只有创建日志」时无法判断是被跳过还是不在领地内。

## 1.1.0

### 新增

- **补齐脚本生成路径（第三通道）**
  之前只拦住了 `BedrockSpawner::spawnMob` 这条路径（自然刷怪 / 刷怪笼 / `/summon` / 刷怪蛋）。
  很多 Addon（如驯龙高手）用脚本 API `dimension.spawnEntity()` 直接创建实体，**完全绕过该函数**，
  于是出现「名单里有、`/tmb test` 显示会被拦、实际没拦、控制台也没有任何日志」的现象。
  本次挂钩所有创建路径的公共汇合点 `Level::$addEntity`：引擎完成创建后取 `Actor*` 判定，
  命中则入队、下一 tick 安全清除。

- **三条通道共用同一判定**：`SpawnJudge` 统一做「名单匹配 → 领地查询 → `allowMonsterSpawn` 判定」，
  避免不同通道行为漂移。

- **新增配置（结构版本 v2 → v3，自动迁移）**
  - `hook_actor_add`（默认 `true`）：是否启用实体创建挂钩；
  - `hook_skip_tamed`（默认 `true`）：命中时跳过已驯服生物，避免清掉玩家的宠物；
  - `hook_remove_delay_ticks`（默认 `1`）：命中后延迟几 tick 清除。

- **诊断增强**
  - `log_level >= 4` 时输出 `[ActorAdd] 实体创建: <id>` 与 `[判定] ... => 拦截/放行`，
    下次出现「没拦住」可直接从日志区分是**挂钩没触发**还是**判定放行**；
  - `/tmb list` 增加「脚本生成拦截(实体创建挂钩)」状态。

### 说明

本插件**只拦截生成**，不清扫领土里已经存在的生物（有意设计，避免误删玩家已驯养的生物）。

## 1.0.1

构建基建更新，插件运行期行为与 1.0.0 完全一致。

### 工程

- **GitHub Actions 自动构建与发版**
  - `build.yml`：push 到 `main` / PR / 手动触发，构建并上传 artifact；
  - `release.yml`：推送 `v*` tag 自动构建 → 打包 `TerritoryMobBlocker-<ver>.zip` →
    创建或更新 Release，正文从本文件对应版本段落提取，带连字符的 tag 自动标记预发布；
  - 版本号由 tag 派生（`TMB_VERSION` 注入 `xmake.lua`），打 tag 即版本号的唯一来源，
    本地构建未设置时回退 `1.0.0`。
- **改用预编译 SDK 分发，绕过上游构建故障**
  上游 LeviLamina 26.51.x 目前无法从源码编译：`mc/server/commands/MinecraftCommands.h`
  用 `unique_ptr<T>` 保存了仅前向声明的 `T`，同时声明 `virtual ~MinecraftCommands() = default;`，
  任何实例化该析构函数的翻译单元都会触发 MSVC STL 的断言
  `invalid application of 'sizeof' to an incomplete type`（clang 19/22、MSVC 14.44/14.51 均可复现）。
  因此新增 `xmake-repo-extra/packages/l/levilamina/xmake.lua`：与官方包同名同结构，只把
  `on_install` 换成下载预编译 SDK（`sdk-v26.51.6` Release 资产），依赖、define、linkdirs
  保持一致，`@levibuildscript/linkrule` 的 prelink 流程不受影响。
  上游修好后删掉该目录并移除 `add_repositories` 即可回到官方构建。

## 1.0.0

首个可用版本。

### 新增

- **双通道拦截**
  - 前置 `ll::event::SpawningMobEvent`：在 `BedrockSpawner::spawnMob` 执行前 `cancel()`，不产生实体对象，零网络包开销（主通道）。
  - 后置 `ll::event::SpawnedMobEvent`：对刷怪蛋 / 指令 / 刷怪笼 / 组件骑手等绕过前置事件的路径 `mob->despawn()` 兜底。
- **领地环境权限联动**：通过 PLand 的 `LandRegistry::getLandAt` 定位领地，读取 `LandPermTable::environment.allowMonsterSpawn`；仅在领地关闭怪物生成时拦截。
- **灵活的生物 ID 匹配**：完整 ID、简写 ID、命名空间通配（`ns:*`）、前缀通配（`prefix_*`），大小写不敏感，自动剥离 `<event>` 后缀。
- **豁免名单**：`exempt_mobs` 可让个别生物无视黑名单强制放行。
- **一键非原版模式**：`block_all_non_vanilla_mobs` 免去逐条配置。
- **管理指令**：`/tmb list`、`/tmb reload`、`/tmb probe`、`/tmb test <mobId>`。
- **配置热重载**：修改名单无需重启服务器。

### 工程

- **修复：`/tmb` 子命令全部不可用（`语法错误:意外的"list"`）**
  `ll::command` 的 `overload<T>()` **不会**自动把结构体成员注册成命令参数，必须显式调用
  `.text()` / `.required()` / `.optional()` 逐个声明。原实现只写了 `overload<T>()`，
  导致注册出来的是**无参数重载**，于是任何子命令都会被判定为多余 token。
  现改为字面量子命令（`/tmb`、`/tmb list`、`/tmb reload`、`/tmb probe`、`/tmb test <mobId>`），
  彻底去掉 enum 参数，也不再依赖 `tryRegisterEnum` 的注册时序。
  > 附带约束：命令参数结构体必须定义在**具名命名空间**（不能用匿名命名空间），
  > 否则 boost::pfr 反射会因类型无外部链接而编译失败。
- **指令权限明确为全局 OP**：`CommandPermissionLevel::GameDirectors`（`/op` 授予的 operator
  权限），并在每个子命令入口再做一次 `getPermissionsLevel()` 校验；领地主人 / PLand 管理员
  不获得额外放行。
- 指令注册用 try/catch 包裹：注册失败只影响管理指令，不影响拦截主体功能。
- **配置文件版本升级自动化（v1 → v2）**
  - 新增结构版本号 `kCurrentConfigVersion`（当前 `2`），启动时自动比对并迁移；
  - **就地改值优先**：只替换需要变更的标量字段（`version` / `log_blocked_spawns` / `log_level`），
    用户手写的注释与排版完整保留；
  - **写得进才写**：改写后的文本会重新解析并与内存配置逐字段比对，不一致就放弃就地升级；
  - 缺字段 / 结构变化时回退整文件重写，并先把原文件备份为 `config.jsonc.bak`；
  - `version` 高于插件时只警告、不改写文件；
  - 迁移原则：只调整「仍沿用旧默认值」的字段，用户显式改过的值一律不动；
  - 迁移逻辑抽到 `Config/ConfigMigration.*`（不依赖日志/Mod 运行时），配有独立单测。
- **默认日志策略调整**
  - `log_level` 默认改回 `3`（Info）：启动自检、配置摘要等关键信息可见；
  - `log_blocked_spawns` 默认改为 `false`：逐条拦截日志默认关闭，避免领地多、刷怪频繁时刷屏；
    排查时单独打开即可（无需改动日志等级）。
- **配置文件改为 JSONC（`config/config.jsonc`）**
  - 首次运行生成的默认配置**每个字段都带中文注释**，并写明四种 ID 写法与注意事项；
  - 解析使用 `ignore_comments = true` + `ignore_trailing_commas = true`，支持 `//`、`/* */` 与末尾多余逗号；
  - 旧版 `config.json` **自动迁移**：读取 → 生成 `config.jsonc` → 旧文件重命名为 `config.json.bak`；
  - 加载成功后不再回写配置，用户的注释与排版不会被冲掉；
  - 渲染逻辑抽到 `Config/ConfigTemplate.*`（不依赖日志/Mod 运行时），便于单元测试。
- **修复：自检时机错误导致误判「PLand 未启用」**
  LeviLamina 是「先加载全部 Mod，再逐个 enable」，在 `load()` 阶段 `Mod::isEnabled()` 必然为 false，
  原实现据此直接判定失败。现改为：
  - 自检从 `load()` 移到 `enable()`（此时 manifest 中的强依赖 PLand 已完成 enable）；
  - `probe()` 只在 PLand 被**显式禁用**时拒绝，其余情况以「导出符号是否齐全」作为唯一判定；
  - 新增 `PlandBridge::ensureAvailable()`：接口不可用时按 5 秒冷却惰性重探，覆盖热重载/加载时序异常。
- 采用**运行期符号解析**（`ModManagerRegistry` + `GetProcAddress`）访问 PLand，不在链接期依赖 `pland` SDK：
  - 规避了 xmake-repo 中 `pland` 包需要从源码构建整个 PLand（abseil / exprtk / imgui 等）导致的构建失败；
  - PLand 升级或符号缺失时插件仍能正常加载，只是降级为「不拦截」并打印自检报告，而不是整个 DLL 加载失败。
- 导出符号已对 PLand `0.22.2` 与 `0.25.0` 双向核对一致。
- 环境权限字段布局在**首次读取时自动校验**（三个 bool 必须为 0/1），异常则 fail-open 降级，避免在允许生成的领地误杀实体。
- `levilamina` 锁定 `26.51.6`：`26.51.9` 的 SDK 包在当前工具链下自身编译失败（`MinecraftCommands` 前置声明不完整）。
- `modpacker` 规则显式声明 `modVersion`，避免非 git 目录触发 `git describe` 报错。
