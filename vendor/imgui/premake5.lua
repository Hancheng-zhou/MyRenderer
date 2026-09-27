-- ===========================================================================
--  ImGui (dear imgui) - premake5 构建脚本
--
--  只编译核心 + 真正用到的两个后端（GLFW 平台 / OpenGL3 渲染）。
--  backends/ 下还有 DX / Vulkan / SDL / Metal 等十几个后端，留在目录里但
--  不参与编译，否则会拉进一堆当前工程根本没有的依赖。
--
--  GL loader 说明：
--    imgui_impl_opengl3.cpp 自带一个精简版 loader（imgui_impl_opengl3_loader.h，
--    基于 gl3w），会自己加载所需的 GL 函数指针。这与主工程用的 glad 互不冲突，
--    官方文档也明确支持这种混合用法，所以这里不需要改任何配置。
-- ===========================================================================

project "ImGui"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	-- 必须和主工程一致：静态 CRT。不一致会导致链接时 CRT 冲突
	staticruntime "on"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	includedirs
	{
		".",                 -- imgui.h / imgui_internal.h
		"backends",          -- imgui_impl_*.h
		"../GLFW/include",   -- imgui_impl_glfw.cpp 需要 GLFW/glfw3.h 与 glfw3native.h
	}

	files
	{
		-- 核心
		"imconfig.h",
		"imgui.h",
		"imgui_internal.h",
		"imstb_rectpack.h",
		"imstb_textedit.h",
		"imstb_truetype.h",
		"imgui.cpp",
		"imgui_draw.cpp",
		"imgui_tables.cpp",
		"imgui_widgets.cpp",
		"imgui_demo.cpp",    -- 提供 ShowDemoWindow()，验证接入是否成功的现成工具

		-- 后端
		"backends/imgui_impl_glfw.h",
		"backends/imgui_impl_glfw.cpp",
		"backends/imgui_impl_opengl3.h",
		"backends/imgui_impl_opengl3.cpp",
		"backends/imgui_impl_opengl3_loader.h",
	}

	filter "system:windows"
		systemversion "latest"
		-- GLFW 默认会自己 include 系统 GL 头（<GL/gl.h>），与 OpenGL3 后端的
		-- 自带 loader 重复声明同一批符号。这个宏让 GLFW 只保留窗口/输入部分
		defines { "GLFW_INCLUDE_NONE" }

	filter "system:linux"
		pic "On"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"

	filter {}
