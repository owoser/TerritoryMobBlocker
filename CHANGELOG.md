# 更新日志

## 未发布

### 构建

- **移除预编译 SDK 分发，回归官方 `levimc-repo` 源码构建**
  - 删除 `xmake-repo-extra/`（把 levilamina 的 `on_install` 换成下载预编译 SDK 的本地包定义）
    与 `tools/build-sdk.ps1`，`xmake.lua` 不再注册 `tmb-extra` 仓库。
  - 起因：这套机制原本是为了绕开 clang 19 编译 `MinecraftCommands.h` 时的
    `invalid application of 'sizeof' to an incomplete type`（LLVM 的 bug）。clang-cl 23 早已修复，
    源码构建不再是「绕开失败」，而只是省时间——本机实测 configure（拉 LL 源码 + 编译本体）**3 分 40 秒**，
    装好的包落在 `~/.xmake/packages` 并被 `actions/cache` 复用，只有 LL 版本号变化时才重编一次。
  - 顺带消除一类风险：整段替换 `on_install` 意味着上游改依赖我们不会自动跟上
    （此前 `parallel-hashmap` 就停留在 v1.3.12、落后官方 26.60.2 的 v2.0.0），
    也不再需要为每次 LL 升级手工重打包 11MB 二进制 + 双写 sha256。
  - CI 缓存 key 随之简化为 `hashFiles('xmake.lua')`。

## 2.0.0

适配 **LeviLamina 26.60**（服务端 Bedrock 26.60.6）。这是一次**破坏性升级**：本版本只支持 26.60.x，
仍在 26.51.x 的服务端请继续使用 1.2.0。

### 变更

- **升级到 LeviLamina 26.60.2**
  - `tooth.json` 依赖 `github.com/LiteLDev/LeviLamina` 由 `26.51.*` 改为 `26.60.*`，
    `github.com/IceBlcokMC/PLand` 由 `0.22.* || 0.23.* || 0.24.* || 0.25.*` 改为 `0.26.*`；
  - 插件主版本号升到 **2.0.0**：依赖线一改，lip 就不会再把本版本装到 26.51 服务端上。
- **安装目录改为 `mods/`**
  LeviLamina 26.60 起服务端 Mod 目录由 `plugins/` 改为 `mods/`，同时 `tooth.json` 补上此前为空的
  `assets`（原先 lip 安装其实不会落地任何文件）：
  `placements = [{ type = "dir", src = "TerritoryMobBlocker/", dest = "mods/TerritoryMobBlocker" }]`。
- **MCAPI 复核结果：无需申请新权限**
  26.60.2 把 `Level.h` 的 `MCNAPI` 标记从 0 处提高到 410 处（整个 `include/mc` 共 55045 处），
  本插件涉及的符号仍然全部可用：`Level::$addEntity`、`Actor::getOrCreateUniqueID`、`Actor::isTame`
  都是 `MCAPI`（不是 `MCNAPI`）；`Actor::isAlive` / `Level::fetchEntity` / `Level::getRuntimeActorList`
  是虚函数，走 vtable 不产生导入符号，只要不写成限定调用（`Level::fetchEntity(...)` 或 `&Level::fetchEntity`）就不会踩坑。
  全量重新编译 12 个翻译单元：**0 warning / 0 error**，没有出现任何 `deprecated` 提示。

### 工程

- **预编译 SDK 升级到 `sdk-v26.60.2`**
  <https://github.com/owoser/TerritoryMobBlocker/releases/tag/sdk-v26.60.2>，
  SHA256 `4af4fa41e679d4f5912e9bf9258984b60278e517d3e77c41a99314cb6fe53c0e`。
- **`tools/build-sdk.ps1` 按 `target_type` 精确选包**
  同一版本在 `.xmake/packages/l/levilamina/<ver>` 下会同时存在 server / client 两份安装（哈希目录不同），
  原先直接取第一项，可能把 client SDK 打成 server 包；现在按 `manifest.txt` 里的 `target_type` 匹配。
- **本地包依赖对齐官方 `versions/26_60_2.lua`**
  `parallel-hashmap` `v1.3.12` → `v2.0.0`（26.51.6 时代遗留的偏差，官方 26.51.9 起就是 v2.0.0），
  `bedrockdata` → `v26.60.6-server.8` / `v26.60.5-client.8`。
- **PLand 0.26.0 兼容性复核**
  6 个导出符号的 mangled 名完全未变（`LandID = int64_t`、`LandDimid = int` 未变）；
  `LandPermTable` 仍以 `EnvironmentPerms` 开头，前三个 bool 仍是
  `allowFireSpread` / `allowMonsterSpawn` / `allowAnimalSpawn`，权限表偏移校验照常通过。

---

## 1.2.0

### 新增

- **新实体监视器（兜底通道，与生成路径无关）**
  实测发现「手动 `/summon` 能拦、Addon 自己召唤却一条日志都没有」：引擎里创建实体的入口不止
  `Level::$addEntity` 一个（还有 `$putEntity` 的两个重载、`$addGlobalEntity`、`$addAutonomousEntity` 等），
  逐个挂钩无法保证覆盖所有脚本 API。监视器改为**只认「新出现的实体」**，彻底绕开"走哪条路径"的问题：
  - 首次扫描把当前已有的实体登记为**基线（存量）**，此后永不处理；
  - 之后每 `watch_interval_ticks`（默认 4 tick ≈ 0.2 秒）扫描一次，凡是从未见过的实体都按同一套判定处理；
  - 每轮用当前实体列表重建集合，已消失的实体自动淘汰，内存不会增长。

  代价是最多 0.2 秒的清理延迟；`watch_skip_tamed` 默认 `true`，已驯服的宠物不会被动。

- 新增配置（结构版本 v3 → v4，自动迁移）：
  `watch_new_entities`（默认 true）、`watch_interval_ticks`（默认 4）、
  `watch_skip_tamed`（默认 true）、`watch_log_all_new_entities`（默认 false）。

- `/tmb list` 增加「新实体监视器」状态。

### 说明

监视器在**启动时扫描到的实体视为存量**。若某些旧生物所在区块在启动时未加载，
它们会在区块加载后被当作新实体处理 —— 已驯服的生物因 `watch_skip_tamed` 默认跳过而不受影响。

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
