#include "Window.h"

#include <cstdio>
#include <stdexcept>
#include <utility>

namespace
{
	// glfwSetErrorCallback 是【进程全局】的，不属于任何窗口实例。
	// 所以这里用自由函数而不是成员函数：如果绑定了某个 Window 的 this，
	// 那个窗口析构之后 GLFW 再报错就是悬垂指针访问。
	void HandleGlfwError(int error, const char* description)
	{
		std::fprintf(stderr, "[GLFW] error %d: %s\n", error, description);
	}
}

// GLFW 的 init/terminate 本身不做引用计数，语义得自己维护：
// 第一个窗口负责 glfwInit，最后一个窗口负责 glfwTerminate。
static uint32_t s_GLFWWindowCount = 0;

Window::Window(const WindowProps& props)
	: m_Data(props)
{
	if (s_GLFWWindowCount == 0)
	{
		// 必须早于 glfwInit：否则初始化阶段的错误收不到回调
		glfwSetErrorCallback(HandleGlfwError);

		if (glfwInit() == GLFW_FALSE)
		{
			throw std::runtime_error("[GLFW] glfwInit failed");
		}
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	// 不设 PROFILE 的话，Windows 上会拿到兼容上下文：
	// #version 330 core 的着色器照样能编译，所以这个错误极难被发现，
	// 但核心模式的约束（必须绑定 VAO 等）会全部失效。
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	m_Window = glfwCreateWindow(static_cast<int>(m_Data.Width),
	                            static_cast<int>(m_Data.Height),
	                            m_Data.Title.c_str(),
	                            nullptr, nullptr);
	if (m_Window == nullptr)
	{
		// 构造函数抛异常时析构函数不会执行，
		// 所以这里必须自己把已经拿到的资源（glfwInit）清理掉。
		if (s_GLFWWindowCount == 0)
		{
			glfwTerminate();
		}
		throw std::runtime_error("[GLFW] glfwCreateWindow failed");
	}
	++s_GLFWWindowCount;   // 必须放在创建成功之后，否则失败时计数虚高

	glfwMakeContextCurrent(m_Window);
	glfwSwapInterval(1);   // 垂直同步：避免空转把 GPU 打满

	// 缓存 framebuffer 尺寸（像素），而不是窗口尺寸（屏幕坐标）：
	// 在 125% / 150% 缩放的显示器上两者不相等，用错会导致画面被拉伸。
	int framebufferWidth  = 0;
	int framebufferHeight = 0;
	glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
	m_Width  = static_cast<uint32_t>(framebufferWidth);
	m_Height = static_cast<uint32_t>(framebufferHeight);

	InstallCallbacks();
}

Window::~Window()
{
	if (m_Window != nullptr)
	{
		glfwDestroyWindow(m_Window);
		m_Window = nullptr;
	}

	if (--s_GLFWWindowCount == 0)
	{
		glfwTerminate();
	}
}

void Window::InstallCallbacks()
{
	// 每个窗口只有这一个 void* 槽位，跳板靠它把 GLFWwindow* 反查回 Window*。
	// 注意这个槽位是【独占】的：以后别的模块也想挂数据时，
	// 要改成挂一个 WindowData 结构体，而不是直接挂 this。
	glfwSetWindowUserPointer(m_Window, this);

	// 下面每个 lambda 都【不能有捕获】，否则无法隐式转换成 GLFW 要求的 C 函数指针。
	// lambda 定义在成员函数内部，因此访问 self 的 private 成员是合法的。

	glfwSetKeyCallback(m_Window,
		[](GLFWwindow* handle, int key, int /*scancode*/, int action, int /*mods*/)
		{
			Window& self = *static_cast<Window*>(glfwGetWindowUserPointer(handle));
			if (self.m_KeyCallback) { self.m_KeyCallback(key, action); }
		});

	glfwSetMouseButtonCallback(m_Window,
		[](GLFWwindow* handle, int button, int action, int /*mods*/)
		{
			Window& self = *static_cast<Window*>(glfwGetWindowUserPointer(handle));
			if (self.m_MouseButtonCallback) { self.m_MouseButtonCallback(button, action); }
		});

	glfwSetCursorPosCallback(m_Window,
		[](GLFWwindow* handle, double x, double y)
		{
			Window& self = *static_cast<Window*>(glfwGetWindowUserPointer(handle));
			if (self.m_CursorPosCallback) { self.m_CursorPosCallback(x, y); }
		});

	glfwSetScrollCallback(m_Window,
		[](GLFWwindow* handle, double xoffset, double yoffset)
		{
			Window& self = *static_cast<Window*>(glfwGetWindowUserPointer(handle));
			if (self.m_ScrollCallback) { self.m_ScrollCallback(xoffset, yoffset); }
		});

	// 必须用 FramebufferSize 而不是 WindowSize：前者是像素，后者是屏幕坐标。
	// 窗口从一个缩放比例的显示器拖到另一个时，WindowSize 不变而 FramebufferSize 会变，
	// 且两个回调都不会触发 —— 所以主循环里每帧比对一次尺寸是更稳的做法。
	glfwSetFramebufferSizeCallback(m_Window,
		[](GLFWwindow* handle, int width, int height)
		{
			Window& self = *static_cast<Window*>(glfwGetWindowUserPointer(handle));

			// 先更新缓存再通知外部：这样回调里调 GetWidth() 拿到的也是新值。
			// 注意窗口最小化时 width/height 会是 0，外部算 aspect 时必须防护除零。
			self.m_Width  = static_cast<uint32_t>(width);
			self.m_Height = static_cast<uint32_t>(height);

			if (self.m_ResizeCallback) { self.m_ResizeCallback(self.m_Width, self.m_Height); }
		});
}

bool Window::ShouldClose() const
{
	return glfwWindowShouldClose(m_Window) == GLFW_TRUE;
}

void Window::SetShouldClose(bool value)
{
	glfwSetWindowShouldClose(m_Window, value ? GLFW_TRUE : GLFW_FALSE);
}

void Window::PollEvents()
{
	glfwPollEvents();
}

void Window::SwapBuffers()
{
	glfwSwapBuffers(m_Window);
}

uint32_t Window::GetWidth() const
{
	return m_Width;
}

uint32_t Window::GetHeight() const
{
	return m_Height;
}

bool Window::IsKeyPressed(int key) const
{
	return glfwGetKey(m_Window, key) == GLFW_PRESS;
}

bool Window::IsMouseButtonPressed(int button) const
{
	return glfwGetMouseButton(m_Window, button) == GLFW_PRESS;
}


// setter 只往 std::function 里填内容，【不要】再调一次 glfwSetXxxCallback：
// 跳板在构造时就已经常驻，重装一遍是多余的，而且 setter 在构造前调用会失败。

void Window::SetKeyCallback(std::function<void(int, int)> cb)
{
	m_KeyCallback = std::move(cb);
}

void Window::SetMouseButtonCallback(std::function<void(int, int)> cb)
{
	m_MouseButtonCallback = std::move(cb);
}

void Window::SetCursorPosCallback(std::function<void(double, double)> cb)
{
	m_CursorPosCallback = std::move(cb);
}

void Window::SetScrollCallback(std::function<void(double, double)> cb)
{
	m_ScrollCallback = std::move(cb);
}

void Window::SetResizeCallback(std::function<void(uint32_t, uint32_t)> cb)
{
	m_ResizeCallback = std::move(cb);
}

void Window::SetRawMouseMode(bool enabled)
{
	glfwSetInputMode(m_Window, GLFW_CURSOR,
	                 enabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

double Window::GetTime()
{
	return glfwGetTime();
}
