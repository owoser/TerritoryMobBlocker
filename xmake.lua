add_rules("mode.debug", "mode.release")

if is_plat("windows") then
    set_toolchains("clang-cl")
end

-- 本地补丁仓库：把 levilamina 的 on_install 换成「预编译 SDK」下载，免去每次编译上游本体。
--
-- 上游 LeviLamina 26.60.2 在 clang-cl 23 下可从源码构建（clang 19 会触发
-- MinecraftCommands.h 的 incomplete type 错误），所以预编译 SDK 只是构建加速手段
-- （上游本体上千个 TU，CI 上要十几分钟），不是绕开编译失败的补丁。
-- 详见 xmake-repo-extra/packages/l/levilamina/xmake.lua。
-- 必须放在 levimc-repo 之前：同名包以先注册者为准。
add_repositories("tmb-extra xmake-repo-extra")
add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- 说明：LeviLamina SDK 版本线需与服务器实际部署的 LL3 保持同一小版本线。
-- 这里锁定 26.60.2（clang-cl 23 构建、本机验证可用的版本），不使用浮动版本，
-- 避免上游小版本升级悄悄改变 ABI / 依赖版本。
if is_config("target_type", "server") then
    add_requires("levilamina 26.60.2", { configs = { target_type = "server" } })
else
    add_requires("levilamina 26.60.2", { configs = { target_type = "client" } })
end

-- 强制所有传递依赖使用同一份 levilamina，避免 ABI 混用
add_requireconfs("**.levilamina", { override = true, version = "26.60.2", configs = { target_type = "server" } })

add_requires("levibuildscript")

-- 说明：PLand 是运行期插件，本插件不在链接期引用它的导入库：
--   * xmake-repo 的 pland 包需要从源码构建整个 PLand（依赖 abseil / exprtk / imgui 等），
--     构建代价高且易失败；
--   * 硬链接导入库会让「PLand 缺符号」直接变成「本插件 DLL 加载失败」。
-- 因此改为启动时通过 ModManagerRegistry + GetProcAddress 动态解析所需导出符号
-- （详见 src/Pland/PlandBridge.cpp），缺失时安全降级为不拦截并打印自检报告。

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

-- 版本号：CI 通过环境变量 TMB_VERSION 注入（取自 tag，如 v1.1.0），本地构建回退到 1.0.0。
-- 这样打 tag 就是版本号的唯一来源，不用同时改 xmake.lua / tooth.json。
local tmbVersion = (os.getenv("TMB_VERSION") or "1.0.0"):gsub("^[vV]", "")

target("TerritoryMobBlocker")
    set_default(true)
    add_rules("@levibuildscript/linkrule")

    -- modVersion 显式声明：避免 modpacker 规则回退到 `git describe`（非 git 目录会直接报错）
    -- pluginName / modName 同时提供：不同版本 modpacker 规则的模板变量名不一致
    add_rules("@levibuildscript/modpacker", {
        pluginName = "TerritoryMobBlocker",
        modName    = "TerritoryMobBlocker",
        modFile    = "TerritoryMobBlocker.dll",
        modVersion = tmbVersion
    })

    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE", "WIN32_LEAN_AND_MEAN")
        set_exceptions("cxx")
        add_cxflags("/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            { tools = { "clang_cl" } }
        )
    end

    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++23")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")

    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
    else
        add_defines("LL_PLAT_C")
    end

    -- 护栏：LeviLamina 26.51+ 的 MinecraftCommands.h 在 clang 19 上会触发
    --   error: invalid application of 'sizeof' to an incomplete type
    -- 必须用 clang-cl >= 23。VS 2022 自带的 Clang 组件只有 19.1.5，如果 PATH 里
    -- 先命中了它（例如在 VS 开发人员命令提示符里构建），这里直接给出可读的报错，
    -- 而不是等编译上游头文件时才炸。
    on_load(function (target)
        if not is_plat("windows") then
            return
        end
        local clangCl = nil
        for _, dir in ipairs(path.splitenv(os.getenv("PATH") or "")) do
            local exe = path.join(dir, "clang-cl.exe")
            if os.isfile(exe) then
                clangCl = exe
                break
            end
        end
        if not clangCl then
            return
        end
        local major = os.iorunv(clangCl, {"--version"}):match("clang version (%d+)")
        if major and tonumber(major) < 23 then
            raise(
                "LeviLamina 26.60.2 需要 clang-cl >= 23，当前 PATH 里先找到的 clang-cl 是 %s.x（%s）。\n" ..
                "请安装 LLVM 23（https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.4），\n" ..
                "并把它的 bin 目录放到 PATH 最前面；VS 2022 自带的 19.1.5 不够。",
                major, clangCl
            )
        end
    end)
