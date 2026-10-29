# TerritoryMobBlocker

[![Build](https://github.com/owoser/TerritoryMobBlocker/actions/workflows/build.yml/badge.svg)](https://github.com/owoser/TerritoryMobBlocker/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/owoser/TerritoryMobBlocker)](https://github.com/owoser/TerritoryMobBlocker/releases)

> 基于 **LeviLamina 26.51.\*** 的原生插件，用于补全 **PLand** 领地系统中「环境权限 → 怪物生成」对 **Addons 自定义生物** 的拦截盲区。

---

## 1. 为什么需要这个插件

PLand 在实现「领地关闭怪物生成」时，两处判定都写死了原版生物标识，导致 Addons 生物被排除在外：

| 判定层 | PLand 的做法 | 对 Addons 生物的后果 |
| --- | --- | --- |
| 前置 `SpawningMobEvent` | 读取实体定义里的 `mSpawnCategory`，只处理 `SpawnCategory::Monster` | Addon 生物通常没有 `population_control: monster`，分类落到 `Misc`/`Ambient`/`None`，走 `default: 放行` |
| 后置 `SpawnedMobEvent` | `mob->hasCategory(ActorCategory::Monster) \|\| mob->hasFamily("monster")` | Addon 生物多为自定义 category / family，被判定为「非怪物」，在 `allowAnimalSpawn` 默认开启时放行 |

**本插件做的事**：独立监听 LeviLamina 的生成事件，在生物 ID 命中配置名单、且所在地领地的 `allowMonsterSpawn == false` 时拦截该次生成。

它与 PLand 的判断逻辑互补、互不干扰：原版怪物仍由 PLand 处理，Addons 生物由本插件补齐。

---

## 2. 工作原理

```
BedrockSpawner::spawnMob
        │
        ├─► [LL] SpawningMobEvent  ──► 本插件：命中名单 + 领地禁怪 ⇒ cancel()  ← 主通道，零实体开销
        │
        └─► 实体创建
                │
                └─► [LL] SpawnedMobEvent ──► 本插件：命中名单 + 领地禁怪 ⇒ mob->despawn()  ← 兜底通道
```

1. **匹配**：先对生物 ID 做规范化（去 `<event>` 后缀、去空白、转小写），再与配置规则比对。**不命中的生物直接放行，不做任何领地查询**，因此热路径开销接近零。
2. **查领地**：调用 `LandRegistry::getLandAt(BlockPos, dimId)` 获取坐标所在领地（子领地优先），并读取 `LandPermTable::environment.allowMonsterSpawn`。
3. **判定**：仅当 `allowMonsterSpawn == false` 时拦截；荒野或允许生成的领地一律放行。

---

## 3. 安装

1. 服务器需为 **LeviLamina 26.51.x**，并已安装 **PLand ≥ 0.22**（manifest 中已声明为强依赖，LL 会保证加载顺序）。
2. 将 `bin/TerritoryMobBlocker/` 整个目录拷到服务器的 `plugins/` 下：

```
plugins/
└── TerritoryMobBlocker/
    ├── TerritoryMobBlocker.dll
    ├── manifest.json
    └── config/
        └── config.jsonc     ← 首次启动自动生成（带注释）
```

3. 重启服务器。控制台应出现：

```
[TerritoryMobBlocker] 正在加载 TerritoryMobBlocker（PLand 领地自定义生物生成拦截）...
[TerritoryMobBlocker] 配置加载完成：拦截名单 2 条，豁免名单 0 条
[TerritoryMobBlocker] PLand 接口自检通过（v0.25.0），拦截规则已生效
[TerritoryMobBlocker] 领地自定义生物生成拦截监听器已注册就绪
[TerritoryMobBlocker] TerritoryMobBlocker 已成功启用
```

> 默认 `log_level: 3`（Info）即可看到以上全部内容；嫌日志多可降到 `1`（仅错误）。

---

## 4. 配置

路径：`plugins/TerritoryMobBlocker/config/config.jsonc`

**格式为 JSONC**（JSON with Comments）——允许 `//` 行注释与 `/* */` 块注释，也容忍末尾多余的逗号。

- 首次启动会生成一份**每个字段都带中文注释**的默认配置；
- 从旧版 `config.json` 升级时**自动迁移**：读取旧配置 → 生成带注释的 `config.jsonc` → 旧文件重命名为 `config.json.bak`；
- **配置结构版本升级会自动完成**（详见 4.4），不需要手工对照新模板改文件；
- 加载成功后插件**不会回写**配置，你的注释与排版不会被冲掉（仅在版本升级时才写）。

```jsonc
{
    // 插件总开关：false = 完全停用拦截逻辑（不影响服务器启动与其它插件）
    "enabled": true,

    // 拦截名单：领地关闭「怪物生成」时，命中此名单的生物禁止在该领地内生成
    "blocked_mobs": [
        "example_addon:custom_monster",
        "example_addon:boss_*"
    ],

    // 豁免名单：即使命中上面也强制放行
    "exempt_mobs": [],

    "block_all_non_vanilla_mobs": false,   // 一键拦截全部非 minecraft: 生物
    "intercept_natural_spawn": true,       // 拦自然生成
    "intercept_non_natural_spawn": true,   // 拦非自然生成
    "log_blocked_spawns": false,           // 逐条拦截日志（默认关闭，避免刷屏）
    "log_level": 3                         // 默认 3=Info
}
```

### 4.1 日志等级

默认 `log_level: 3`（Info）—— 能看到启动自检、配置加载摘要等关键信息。

逐条拦截日志由**独立开关** `log_blocked_spawns` 控制，**默认 `false`**，所以日常运行不会因为刷怪被刷屏。排查时把它改成 `true` 即可（其日志级别为 Info，因此还需 `log_level >= 3`）。

| 值 | 含义 | 何时用 |
| --- | --- | --- |
| `1` | Error | 只关心出错，完全不想看日志 |
| `3` | Info（默认） | 日常运行：可看到启动自检与配置摘要 |
| `4` | Debug | 排查接口问题：可看到 PLand 符号解析细节 |

> 等级 `1` 时连「启动成功」都不会显示，**只有自检失败这类问题会以 Error 报出来**。

### 4.2 ID 匹配写法

| 写法 | 含义 | 命中示例 |
| --- | --- | --- |
| `my_addon:mutant_creeper` | 完整 ID 精确匹配 | `my_addon:mutant_creeper` |
| `mutant_creeper` | 简写，任意命名空间 | `my_addon:mutant_creeper`、`other:mutant_creeper` |
| `my_addon:*` | 该命名空间下全部生物 | `my_addon:anything` |
| `boss_*` | 全局前缀（任意命名空间） | `my_addon:boss_skeleton`、`dark:boss_skeleton` |
| `my_addon:boss_*` | 指定命名空间前缀 | `my_addon:boss_skeleton` |

规则**大小写不敏感**，并会自动剥离 `<minecraft:as_adult>` 这类事件后缀。

> 只有**结尾**的 `*` 是通配符；`*` 出现在中间会被当作普通字符。

### 4.3 不知道该写哪些 ID？

两种办法：
- 打开 `block_all_non_vanilla_mobs`，让所有非原版生物在禁怪领地内一律被拦（最省心）。
- 先在领地内刷出目标生物，查看控制台日志或服务器实体列表获取实际 ID（`namespace:identifier`），再逐条填入。

### 4.4 配置版本升级（自动）

配置里的 `version` 是**结构版本号**，请勿手改。插件每次启动都会比对：

| 情况 | 行为 |
| --- | --- |
| `version` 与插件一致 | 什么都不做，**不改写文件**（注释、排版、你自己加的说明全部原样保留） |
| `version` 偏低 | **自动迁移并写回**文件，同时把迁移内容打进日志 |
| `version` 偏高 | 警告（配置来自更新的插件），按兼容模式读取，不改写文件 |

自动升级的策略：

1. 只调整「仍沿用旧默认值」的字段，**用户显式改过的值一律不动**；
2. 优先在原文上**就地替换标量值**，因此你的注释与排版会被完整保留；
3. 只有遇到「缺字段 / 结构变化 / 改写后校验不通过」时，才回退到**整文件重写**，并先把原文件备份为 `config.jsonc.bak`；改写前后都会重新解析比对语义，确保不会写出坏配置。

当前版本历史：

| 版本 | 变更 |
| --- | --- |
| v1 | 初版（`log_blocked_spawns` 默认 `true`） |
| v2 | 逐条拦截日志默认改为 `false`；`log_level` 默认回到 `3`（Info） |

升级 v1 → v2 时，若你的 `log_blocked_spawns` 仍是 `true`，会被自动改为 `false`，并在控制台打印：

```
[TerritoryMobBlocker] 配置文件已从 v1 自动升级到 v2（保留原有注释与排版）
[TerritoryMobBlocker]   · log_blocked_spawns: true -> false（v2 起默认关闭，避免逐条拦截日志刷屏）
```

---

## 5. 指令

**权限要求：全局 OP**（由 `/op` 授予的 operator 权限，对应 `CommandPermissionLevel::GameDirectors`）。

- 非 OP 玩家**看不到、也无法执行**这些指令（BDS 在解析阶段就会拒绝）；
- 每个子命令执行前还会再校验一次权限等级（纵深防御，覆盖某些服务端自定义权限映射的情况）；
- **领地主人、PLand 管理员不额外放行** —— 本插件只认全局 OP。

| 指令 | 说明 |
| --- | --- |
| `/tmb` | 等同于 `/tmb list` |
| `/tmb list` | 查看当前开关、规则数量、PLand 接口状态与全部名单条目 |
| `/tmb reload` | 热重载 `config.jsonc`（改完名单无需重启） |
| `/tmb probe` | 重新执行 PLand 导出符号自检，逐条列出 OK/FAIL |
| `/tmb test <mobId>` | 站在某片领地里测试：该生物在此处是否会被拦截，并回显领地权限 |

推荐排查流程：`/tmb probe` → `/tmb test <生物ID>`。若 `test` 显示「环境权限 · 怪物生成: 禁止」且「命中黑名单」→ 判定为拦截，说明整条链路正常。

> 逐条拦截日志额外受 `log_blocked_spawns` 控制（默认 `false`）；启动自检等摘要信息在默认 `log_level: 3` 下可见。

---

## 6. 技术实现要点

### 6.1 不在链接期依赖 PLand

`pland` 的 xmake 包需要从源码构建整个 PLand（依赖 abseil / exprtk / imgui 等），代价高且易失败；硬链接导入库又会让「PLand 换版本」直接变成「本插件 DLL 加载失败」。

因此 `src/Pland/PlandBridge.cpp` 采用**运行期符号解析**：启动时通过 `ll::mod::ModManagerRegistry` 取到 `PLand.dll` 句柄，再用 `GetProcAddress` 逐个解析导出符号。任一符号缺失只会让插件降级为「不拦截」并打印自检报告，不影响服务器启动。

已实测两条版本线完全一致的导出符号：

```
?getInstance@PLand@land@@SAAEAV12@XZ
?getLandRegistry@PLand@land@@QEBAAEAVLandRegistry@2@XZ
?getLandAt@LandRegistry@land@@QEBA?AV?$shared_ptr@VLand@land@@@std@@AEBVBlockPos@@H@Z
?getPermTable@Land@land@@QEBAAEBULandPermTable@2@XZ
?getId@Land@land@@QEBA_JXZ
?getName@Land@land@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ
```

### 6.2 生命周期：自检必须放在 enable 阶段

LeviLamina 的启动顺序是**先把所有 Mod 全部 `load()`，再按依赖拓扑顺序逐个 `enable()`**。因此在 `load()` 阶段，即便是 manifest 里声明为强依赖的 PLand，其 `Mod::isEnabled()` 也仍然是 `false`。

所以本插件的 PLand 接口自检放在 `enable()`：
- 强依赖 PLand 已完成 enable，句柄与符号都可用；
- 万一探测失败，插件仍保持加载状态（不影响服务器启动），并在热路径上通过 `ensureAvailable()`
  以 5 秒冷却惰性重探，可自动从 PLand 热重载或异常加载时序中恢复；
- 也可随时执行 `/tmb probe` 手动重探。

### 6.3 两处容易踩坑的 ABI 细节

1. **返回值传递顺序**：MSVC x64 下，返回非平凡类型（`std::shared_ptr`）的成员函数，**第一个参数是 `this`，第二个才是返回值缓冲区指针**。这一顺序已用本机 clang-cl 工具链实测确认（`tests` 记录见提交说明），写错会导致静默崩溃。
2. **`EnvironmentPerms` 前缀布局**：`LandPermTable` 以 `EnvironmentPerms` 开头，其第 1/2/3 个字段分别是 `allowFireSpread` / `allowMonsterSpawn` / `allowAnimalSpawn`。PLand 官方约定「新增权限只在末尾追加，不在前面插入」，因此前缀偏移恒定。插件在**首次读取领地权限时会自动校验**这三个字节是否都是 `0/1`，异常则判定为布局不兼容并永久降级为「不拦截」（fail-open，绝不在允许生成的领地里误杀实体）。

### 6.4 项目结构

```
TerritoryMobBlocker/
├── xmake.lua                     # 构建配置（levilamina 26.51.6 + levibuildscript，无 pland 依赖）
├── manifest.json                 # 模组清单（强依赖 PLand）
├── tooth.json                    # lip 包元数据
├── xmake-repo-extra/             # 本地 xmake 仓库：levilamina 预编译 SDK 包定义
├── src/
│   ├── Entry/                    # Mod 生命周期 + MemoryOperators
│   ├── Config/
│   │   ├── Config.h              # 配置结构体（聚合类型 + NLOHMANN 默认值）
│   │   ├── ConfigTemplate.*      # 带中文注释的 JSONC 默认配置渲染（无外部依赖，可单测）
│   │   ├── ConfigMigration.*     # 版本迁移：就地改值 / 回退重写（无外部依赖，可单测）
│   │   └── ConfigManager.*       # JSONC 读写、旧格式迁移、版本升级、热重载
│   ├── Interceptor/
│   │   ├── MobMatcher.*          # ID 规范化与四种匹配规则
│   │   └── MobSpawnInterceptor.* # 前置 SpawningMobEvent + 后置 SpawnedMobEvent
│   ├── Pland/PlandBridge.*       # PLand 运行期符号解析与领地权限读取
│   ├── Command/Command.*         # /tmb 指令
│   └── logger.h
└── bin/TerritoryMobBlocker/      # 构建产物（可直接部署）
```

---

## 7. 构建

### 本地构建

```bash
xmake repo -u
xmake f -y -p windows -a x64 -m release
xmake
```

产物：`bin/TerritoryMobBlocker/{TerritoryMobBlocker.dll, TerritoryMobBlocker.pdb, manifest.json}`

> **SDK 分发说明（重要）**：上游 LeviLamina **26.51.x 目前无法从源码编译**——
> `mc/server/commands/MinecraftCommands.h` 用 `unique_ptr<T>` 保存了仅前向声明的 `T`
> （`CommandRegistry` / `CommandOutputSender` / `Command` / `DeferredCommandBase`），
> 同时又声明 `virtual ~MinecraftCommands() = default;`，任何实例化该析构函数的翻译单元
> 都会触发 MSVC STL 的断言 `error: invalid application of 'sizeof' to an incomplete type`。
> 这与编译器版本无关（clang 19/22、MSVC 14.44/14.51 均可复现）。
>
> 因此本项目改为**分发预编译 SDK**：`xmake-repo-extra/packages/l/levilamina/xmake.lua`
> 与官方包同名同结构，只把 `on_install` 换成下载
> [`sdk-v26.51.6`](https://github.com/owoser/TerritoryMobBlocker/releases/tag/sdk-v26.51.6)
> 的 `levilamina-sdk-26.51.6-server.zip`（`include/` + `lib/`，不含运行期 DLL）。
> 依赖列表、define、linkdirs 与官方一致，`@levibuildscript/linkrule` 的 prelink 流程不受影响。
>
> 上游修好后：删掉 `xmake-repo-extra/` 目录，并移除 `xmake.lua` 里的
> `add_repositories("tmb-extra xmake-repo-extra")` 这一行，即回到官方源码构建。

#### 为什么 SDK 是 26.51.6，而不是最新的 26.51.9？

因为**没有** 26.51.9 的 SDK 可以拿到：

| 来源 | 结果 |
| --- | --- |
| 从源码构建（xmake-repo 官方包） | ✗ 26.51.6 与 26.51.9 **都**编译失败，同一个 incomplete type 错误 |
| 官方 Release 附件 | ✗ 只有运行期 DLL，**不含** `include/` 与 `LeviLamina.lib` |
| 官方 Actions 产物 | ✗ 只有文档站（doxygen / pages），没有 SDK 包 |

26.51.6 的 SDK 是这台机器在 2026-10-11 构建成功留下的产物，是唯一可用的 SDK。
它在**符号层面与 26.51.8 / 26.51.9 兼容**：

- 本插件 DLL 从 `LeviLamina.dll` 导入 **37 个**导出符号；
- 逐条比对 26.51.8（线上服务器）与 26.51.9（最新版）的 LeviLamina.dll 导出表，**全部命中**；
- 实测已在 26.51.8 的服务器上正常运行。

等上游修好 `MinecraftCommands.h`、本机能重新 `xmake f` 之后，用
`tools/build-sdk.ps1 -Version 26.51.9` 重新打包 SDK 并更新包定义里的 sha256 即可升级。

### CI / 自动发布

| 工作流 | 触发 | 行为 |
| --- | --- | --- |
| [`build.yml`](.github/workflows/build.yml) | push 到 `main` / PR / 手动 | 配置 + 构建，上传 `TerritoryMobBlocker-windows-x64` artifact 作为构建验证 |
| [`release.yml`](.github/workflows/release.yml) | 推送 `v*` tag / 手动 | 构建 → 打包 `TerritoryMobBlocker-<ver>.zip` → 创建或更新 Release，正文自动从 `CHANGELOG.md` 对应版本段落提取 |

CI 固定 `windows-2022` + `clang-cl 19.1.5`（本地验证通过的组合），并缓存 `~/.xmake`，
首次构建约 5 分钟、命中缓存后更快。

发版流程只剩三步：

```bash
# 1. 在 CHANGELOG.md 顶部加一节 "## 1.1.0"
# 2. 提交
git add -A && git commit -m "chore: release 1.1.0"
# 3. 打 tag 并推送，CI 自动构建 + 出包 + 建 Release
git tag v1.1.0 && git push origin main --tags
```

版本号由 tag 派生（`v1.1.0` → `1.1.0`），通过环境变量 `TMB_VERSION` 注入 `xmake.lua` 写入 `manifest.json`，**不需要手工改版本号**；本地构建未设置该变量时回退为 `1.0.0`。带连字符的 tag（如 `v1.1.0-beta.1`）会自动标记为预发布。

---

## 8. 已知边界

| 场景 | 行为 |
| --- | --- |
| PLand 未安装 / 被禁用 / 版本符号不匹配 | 插件正常加载，打印自检报告，**不拦截任何生物** |
| `EnvironmentPerms` 布局不兼容 | 首次读权限时校验失败，打印错误并永久降级为不拦截 |
| 领主本人 / PLand 管理员 | 无特权豁免 —— 领地禁怪是环境级设置，对所有人一致生效 |
| 已存在的实体 | 本插件只拦「生成」动作，不清理领地内既往生成的生物（重启后由原版 despawn 规则处理） |
| 生物在领地外生成后走进领地 | 不拦截（只拦生成，不做追踪驱逐） |

---

## 9. 版本

- `1.0.0` — 首个版本：前置 + 后置双通道拦截、热重载、运行期符号解析与自检、权限布局自动校验
  - 配置文件改为 **JSONC**（`config.jsonc`），默认文件每个字段带中文注释，旧 `config.json` 自动迁移
  - 默认日志等级改为 `1`（仅 Error），避免拦截日志刷屏
