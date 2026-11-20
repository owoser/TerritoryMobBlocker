-- LeviLamina SDK 的预编译分发包
--
-- 为什么需要它
-- ────────────────────────────────────────────────────────────────────────
-- 本包只替换 on_install（下载现成产物），其余（依赖、define、linkdirs）与官方
-- levimc-repo 定义保持一致，从而不影响 @levibuildscript/linkrule 的 prelink 流程。
-- 目的仅是让本地 / CI 免去每次编译 LeviLamina 本体（clang-cl 23 下源码构建可用，
-- 但一次要编译上千个 TU，CI 上要十几分钟）。
--
-- 历史背景：clang 19 时代上游 26.51.x 无法从源码构建——
--
--   src/mc/server/commands/MinecraftCommands.h 用
--     ::ll::TypedStorage<8, 8, ::std::unique_ptr<T>>
--   保存了仅做前向声明的 T（CommandRegistry / CommandOutputSender / Command /
--   DeferredCommandBase），同时又声明 `virtual ~MinecraftCommands() = default;`。
--   任何实例化该析构函数的 TU（例如 ExecuteCommandEvent.cpp 的
--   LL_TYPE_INSTANCE_HOOK）都会触发 MSVC STL 的断言：
--
--     error: invalid application of 'sizeof' to an incomplete type '...'
--
--   clang 19 必现；升级到 clang-cl 23.1.4 后该 TU 可以正常通过，26.51.9 源码构建实测可用。
--   如果哪天不再需要预编译 SDK 加速，删掉本目录并移除 xmake.lua 里的
--   add_repositories("tmb-extra xmake-repo-extra") 即可回到官方源码构建。
--
-- SDK 由本仓库 Release 分发：
--   https://github.com/owoser/TerritoryMobBlocker/releases/tag/sdk-v26.51.9
-- ────────────────────────────────────────────────────────────────────────

package("levilamina")
    set_homepage("https://github.com/LiteLDev/LeviLamina")
    set_description("LeviLamina SDK (prebuilt, faster than building upstream from source)")

    add_urls(
        "https://github.com/owoser/TerritoryMobBlocker/releases/download/sdk-v$(version)/levilamina-sdk-$(version)-server.zip",
        {alias = "sdk"}
    )
    add_versions("sdk:26.51.9", "33056860fc66676f41d9b11bcef082998a41967ce1ca8a1ea1c9da77861f086c")

    add_defines("ENTT_PACKED_PAGE=128", "ENTT_SPARSE_PAGE=4096", "ENTT_NO_MIXIN")

    add_configs("target_type", {default = "server", values = {"server", "client"}})

    on_load(function (package)
        -- 与 levimc-repo 的 versions/26_51_9.lua 保持一致：
        -- 这些传递依赖仍由 xmake 正常安装，本包只提供 levilamina 自身的 include/lib。
        local deps_common = {
            "entt v4.0.0",
            "expected-lite v0.8.0",
            "fmt 11.2.0",
            "gsl v4.2.0",
            "glm 1.0.1",
            "leveldb 1.23",
            "magic_enum v0.9.7",
            "nlohmann_json v3.12.0",
            "rapidjson 2025.02.05",
            "type_safe v0.2.4",
            "pcg_cpp v1.0.0",
            "pfr 2.1.1",
            "symbolprovider v1.3.0",
            "parallel-hashmap v1.3.12",
            "concurrentqueue v1.0.4",
            "stb 2025.03.14"
        }
        for _, dep in ipairs(deps_common) do
            package:add("deps", dep)
        end

        -- bedrockdata 提供 prelink 阶段需要的 bedrock_runtime_data
        if package:config("target_type") == "server" then
            package:add("deps", "bedrockdata v26.51.1-server.12")
            package:add("defines", "LL_PLAT_S")
        else
            package:add("deps", "bedrockdata v26.51.1-client.12")
            package:add("defines", "LL_PLAT_C")
        end
    end)

    on_install(function (package)
        if package:config("target_type") ~= "server" then
            raise("预编译 SDK 目前只提供 server 变体，请使用 target_type=server")
        end

        local root = os.curdir()
        if not os.isdir(path.join(root, "include")) then
            -- 兼容压缩包带一层顶层目录的情况
            local entries = os.files(path.join(root, "*"))
            if #entries == 1 and os.isdir(entries[1]) then
                root = entries[1]
            end
        end

        for _, name in ipairs({"include", "lib"}) do
            local srcdir = path.join(root, name)
            if not os.isdir(srcdir) then
                raise("SDK 压缩包缺少目录：%s（解压目录：%s）", name, root)
            end
            os.cp(srcdir, package:installdir())
        end

        cprint("${bright green}[levilamina]${clear} 已安装预编译 SDK %s（跳过源码构建）", package:version_str())
    end)
