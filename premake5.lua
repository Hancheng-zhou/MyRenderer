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

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

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
		"vendor/minifb-0.13.0/include",
		"vendor/stb_image",
	}

	links { "GLFW", "GLad", "minifb", "stb_image" }

	filter "system:windows"
		links { "opengl32.lib", "winmm.lib", "gdi32.lib", "user32.lib", "shell32.lib", "ole32.lib" }

	filter {}
