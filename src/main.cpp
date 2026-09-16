// ===========================================================================
//  ZHC - 第 0 层：最小 GLFW 窗口
//
//  目标：把窗口与 OpenGL 上下文跑起来，作为后面各层的编译靶子。
//  本层只做三件事：
//      1. 初始化 GLFW 并创建 3.3 Core Profile 上下文
//      2. 加载 glad，拿到 OpenGL 函数指针
//      3. 清屏事件循环（ESC 退出）
// ===========================================================================

#define GLFW_INCLUDE_NONE

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>

namespace
{
	constexpr int kWindowWidth  = 1280;
	constexpr int kWindowHeight = 720;

	void OnGlfwError(int error, const char* description)
	{
		std::fprintf(stderr, "[GLFW] error %d: %s\n", error, description);
	}

	void OnKey(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
	{
		// 有了这个就不必每次都去点窗口右上角的 X 了
		if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(window, GLFW_TRUE);
		}
	}
}

int main()
{
	glfwSetErrorCallback(OnGlfwError);

	if (glfwInit() == GLFW_FALSE)//初始化GLFW库
	{
		std::fprintf(stderr, "[GLFW] glfwInit failed\n");
		return 1;
	}

	// 要求 4.6 Core Profile：不使用兼容模式，固定管线 API 全部不可用
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	//glfwCreateWindow（宽，高，名称，nullptr表示窗口模式glfwGetPrimaryMonitor()表示全屏，第五个参数：哪个窗口共享 OpenGL 上下文（share context））
	GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "ZHC", nullptr, nullptr);
	if (window == nullptr)
	{
		std::fprintf(stderr, "[GLFW] glfwCreateWindow failed\n");
		glfwTerminate();//释放所有GLFW资源
		return 1;
	}

	// 顺序很重要：glad 必须在当前上下文就绪之后才能加载函数指针
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);   // 垂直同步，避免空转把 GPU 打满 0 表示关闭（帧率不限制） 1表示一帧同步一次 n代表每n次刷新同步一次

	if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
	{
		std::fprintf(stderr, "[GLad] gladLoadGLLoader failed\n");
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	std::printf("[GL] version  : %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
	std::printf("[GL] renderer : %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
	std::printf("[GL] vendor   : %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));

	glfwSetKeyCallback(window, OnKey);
	glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) {
		glViewport(0, 0, w, h);
		});
	while (glfwWindowShouldClose(window) == GLFW_FALSE)
	{
		glfwPollEvents();

		glClearColor(0.09f, 0.11f, 0.14f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glfwSwapBuffers(window);
		

	}

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
