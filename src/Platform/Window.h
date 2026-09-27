// ===========================================================================
//  ZHC - Platform/Window.h
//
//  职责边界：
//    管：GLFW 生命周期、窗口创建/销毁、上下文就绪、原始事件转发、输入状态查询
//    不管：主循环、glad 加载、任何 gl* 调用、渲染状态
//
//  依赖规则：
//    本文件（及整个 Platform/ 目录）不允许出现 <glad/glad.h>。
//    GL 的加载与调用一律在 main / Renderer 层完成。
//
//  生命周期约束（调用方必须遵守）：
//    Window 对象必须先于所有 GL 资源（Shader / VertexBuffer / Texture ...）构造，
//    这样它最后析构，保证 glfwTerminate() 在所有 GL 资源被删除之后才执行。
//    main 里把它声明成第一个局部变量即可。
//
//  构造完成后的契约：
//    GLFW 已初始化、窗口已创建、当前线程已有当前上下文，
//    此时才可以调用 gladLoadGLLoader(glfwGetProcAddress)。
// ===========================================================================

#pragma once

// 必须在 glfw3.h 之前定义，否则 GLFW 会自己 include GL 头，与 glad 冲突。
// （更干净的做法是在 premake5.lua 里用 defines { "GLFW_INCLUDE_NONE" } 全局定义，
//   这样就不必每个头文件都写一遍。两种方式目前都能工作。）
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <cstdint>
#include <functional>
#include <string>

class Window
{
public:
	struct WindowProps
	{
		std::string Title  = "ZHC";
		uint32_t    Width  = 1280;
		uint32_t    Height = 720;
	};

	// ---- 生命周期（必须是 public，否则外部无法构造）--------------------
	explicit Window(const WindowProps& props);
	~Window();
	Window(const Window&)            = delete;
	Window& operator=(const Window&) = delete;

	// ---- 每帧驱动：主循环需要的全部零件 --------------------------------
	bool ShouldClose() const;
	void SetShouldClose(bool value);
	void PollEvents();     // 帧首：分发事件、触发回调
	void SwapBuffers();    // 帧尾：提交这一帧

	// ---- 窗口信息（均为 framebuffer 尺寸，不是窗口尺寸）----------------
	uint32_t GetWidth()  const;
	uint32_t GetHeight() const;

	// ---- 输入轮询：用于"当前是否按住"这类持续状态 ----------------------
	// WASD 移动、右键是否按住，都用这两个，不要用回调 + bool 数组
	bool IsKeyPressed(int key) const;
	bool IsMouseButtonPressed(int button) const;
	
	// ---- 事件注册：用于"按下/弹起/移动/滚动"这类离散事件 ---------------
	// action 是 GLFW_PRESS / GLFW_RELEASE / GLFW_REPEAT 的枚举值，不是 bool；
	// 处理时别把 GLFW_REPEAT 误当成一次新按下。
	void SetKeyCallback(std::function<void(int key, int action)> cb);
	void SetMouseButtonCallback(std::function<void(int button, int action)> cb);
	void SetCursorPosCallback(std::function<void(double x, double y)> cb);
	void SetScrollCallback(std::function<void(double dx, double dy)> cb);
	void SetResizeCallback(std::function<void(uint32_t w, uint32_t h)> cb);

	// ---- 其他 ----------------------------------------------------------
	// 隐藏并锁定光标（FPS 式视角用）。右键拖动旋转视角不需要它
	void SetRawMouseMode(bool enabled);

	// 时间源，转发 glfwGetTime()。放在这里是为了让 main 不必为了取时间而 include glfw3.h
	static double GetTime();

	// 逃逸口：只有 Platform 层没覆盖到的 GLFW 功能才用它
	GLFWwindow* GetNativeHandle() const { return m_Window; }

private:
	// 装配 5 个 GLFW 回调跳板，只在构造函数里调用一次。
	// 跳板靠 glfwSetWindowUserPointer 存 this，再用【无捕获 lambda】隐式转成函数指针。
	void InstallCallbacks();

	GLFWwindow* m_Window = nullptr;
	uint32_t    m_Width  = 0;
	uint32_t    m_Height = 0;
	WindowProps m_Data;

	// 跳板回调的存放位。setter 只写这里，不重装 GLFW 回调
	std::function<void(int, int)>           m_KeyCallback;
	std::function<void(int, int)>           m_MouseButtonCallback;
	std::function<void(double, double)>     m_CursorPosCallback;
	std::function<void(double, double)>     m_ScrollCallback;
	std::function<void(uint32_t, uint32_t)> m_ResizeCallback;
};
