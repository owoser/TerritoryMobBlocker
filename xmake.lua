add_rules("mode.debug", "mode.release")

if is_plat("windows") then
    set_toolchains("clang-cl")
end

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- 说明：LeviLamina SDK 版本线需与服务器实际部署的 LL3 保持同一 26.51.x 小版本线。
-- 这里锁定 26.51.6（本机已缓存、验证可用的构建产物）；26.51.7+ 上游包在当前工具链下
-- 存在自身编译失败（MinecraftCommands 前置声明不完整），故不使用浮动版本。
if is_config("target_type", "server") then
    add_requires("levilamina 26.51.6", { configs = { target_type = "server" } })
else
    add_requires("levilamina 26.51.6", { configs = { target_type = "client" } })
end

-- 强制所有传递依赖使用同一份 levilamina，避免 ABI 混用
add_requireconfs("**.levilamina", { override = true, version = "26.51.6", configs = { target_type = "server" } })

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

target("TerritoryMobBlocker")
    set_default(true)
    add_rules("@levibuildscript/linkrule")

    -- modVersion 显式声明：避免 modpacker 规则回退到 `git describe`（非 git 目录会直接报错）
    -- pluginName / modName 同时提供：不同版本 modpacker 规则的模板变量名不一致
    add_rules("@levibuildscript/modpacker", {
        pluginName = "TerritoryMobBlocker",
        modName    = "TerritoryMobBlocker",
        modFile    = "TerritoryMobBlocker.dll",
        modVersion = "1.0.0"
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
