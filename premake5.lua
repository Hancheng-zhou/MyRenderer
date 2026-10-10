-- ===========================================================================
--  ZHC - premake5 构建脚本
--
--  生成工程（在仓库根目录执行）：
--      vendor\bin\premake\premake5.exe vs2022
--
--  生成的工程：
--      ZHC.sln
--        ├── ZHC       可执行程序（src/）
--        └── vendor
--              ├── GLFW      静态库
--              ├── GLad      静态库
--              ├── stb_image 静态库
--              └── minifb    静态库
--      glm 为纯头文件库，不建工程，直接以包含目录方式使用。
-- ===========================================================================

workspace "ZHC"
	architecture "x86_64"
	startproject "ZHC"
	configurations { "Debug", "Release" }

	-- 输出目录命名，例如 Debug-windows-x86_64
	outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

	-- 公共编译选项
	filter "system:windows"
		systemversion "latest"
		staticruntime "on"
		buildoptions { "/utf-8" }
		-- 多核并行编译（/MP）。
		-- 注意 MSBuild 的 /m 只在【多个项目之间】并行，单个项目内部默认是
		-- 串行的 —— 本工程 17 个编译单元只会用到 1 个核，其余全空着
		--
		-- 用 multiprocessorcompile 而不是老的 flags { "MultiProcessorCompile" }：
		-- 后者已被 premake 标记为弃用，会在每次生成工程时刷两条警告
		multiprocessorcompile "on"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"
		-- 关掉"编辑并继续"，也就是把 /ZI 换成 /Zi。三个后果都很实在：
		--   1. /ZI 会【禁用增量链接】—— 链接器会报 LNK4075 忽略 /INCREMENTAL，
		--      于是每次改完代码都是一次完整链接 + 整份 PDB 重写，
		--      这正是"改代码后第一次 F5 特别慢"的直接来源
		--   2. /ZI 的 PDB 更大，F5 时调试器要把它读进来
		--   3. 本项目从来没用过"调试中改代码"这个能力，纯白付代价
		editandcontinue "off"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"

	filter {}

-- ===========================================================================
--  第三方库
-- ===========================================================================

group "vendor"

	-- GLFW：窗口 / 输入 / 上下文（自带 premake 脚本，仅取 Windows 所需源文件）
	include "vendor/GLFW"

	-- GLad：OpenGL 函数加载器
	include "vendor/GLad"

	-- stb_image：图片解码（单头文件 + 一个实现编译单元）
	include "vendor/stb_image"

	-- ImGui：即时 GUI，用来做调试面板与实时调参。
	-- 自带精简 GL loader，不依赖 glad，两者可以共存
	include "vendor/imgui"

	-- 注意：glm 是纯头文件库，没有可编译的源文件，因此不建工程，
	--       只在下面各工程的 includedirs 中加入 vendor/glm。

	-- minifb：软件帧缓冲窗口
	project "minifb"
		location "vendor/minifb-0.13.0"
		kind "StaticLib"
		language "C++"
		-- minifb 的 Win32 后端使用 char* 调用 Win32 API，必须用多字节字符集编译
		characterset "MBCS"
		targetdir ("bin/" .. outputdir .. "/%{prj.name}")
		objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

		includedirs
		{
			"vendor/minifb-0.13.0/include",
			"vendor/minifb-0.13.0/src",
		}
		files
		{
			"vendor/minifb-0.13.0/include/*.h",
			"vendor/minifb-0.13.0/src/MiniFB_common.c",
			"vendor/minifb-0.13.0/src/MiniFB_cpp.cpp",
			"vendor/minifb-0.13.0/src/MiniFB_internal.c",
			"vendor/minifb-0.13.0/src/MiniFB_internal.h",
			"vendor/minifb-0.13.0/src/MiniFB_timer.c",
			"vendor/minifb-0.13.0/src/WindowData.h",
		}

		filter "system:windows"
			defines
			{
				"_CRT_SECURE_NO_WARNINGS",
				"_WIN32_WINNT=0x0601",
				"USE_OPENGL_API",
			}
			files
			{
				"vendor/minifb-0.13.0/src/windows/WinMiniFB.c",
				"vendor/minifb-0.13.0/src/windows/WindowData_Win.h",
				"vendor/minifb-0.13.0/src/gl/MiniFB_GL.c",
				"vendor/minifb-0.13.0/src/gl/MiniFB_GL.h",
			}
			-- 静态库不在这里链接系统库，由最终的可执行工程负责

		filter {}

group ""

-- ===========================================================================
--  主程序
-- ===========================================================================

project "ZHC"
	location "."
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	-- VS 调试时的工作目录固定为工程目录，这样 assets/... 相对路径可以直接用；
	-- 注意：脱离 VS 双击 exe 运行时工作目录不一定是仓库根，需要自己 cd 或后续加 postbuild 拷贝资源
	debugdir "$(ProjectDir)"
	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.hpp",
		"src/**.c",
		"src/**.cpp",
	}
	includedirs
	{
		"src",
		"vendor/GLFW/include",
		"vendor/GLad/include",
		"vendor/glm",
		"vendor/imgui",             -- imgui.h
		"vendor/imgui/backends",    -- imgui_impl_glfw.h / imgui_impl_opengl3.h
		"vendor/minifb-0.13.0/include",
		"vendor/stb_image",
	}

	links { "GLFW", "GLad", "minifb", "stb_image", "ImGui" }

	filter "system:windows"
		links { "opengl32.lib", "winmm.lib", "gdi32.lib", "user32.lib", "shell32.lib", "ole32.lib" }
		defines
		{
			-- glm 的 gtx 扩展（Transform.h 里的 glm::eulerAngleXYZ 就在其中）默认是关闭的，
			-- 必须显式打开这个宏才能用，否则 gtx 头会直接 #error 报错。
			-- 它还有个硬性要求：必须早于【任何】glm 头被展开，
			-- 所以放在工程级 defines 里最省心 —— 不用去关心每个 cpp 的 include 顺序
			"GLM_ENABLE_EXPERIMENTAL",
		}

	filter {}
