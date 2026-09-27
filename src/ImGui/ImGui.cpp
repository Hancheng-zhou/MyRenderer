#include "ImGui.h"

// 这里必须用【尖括号】：vendor/imgui 是通过 includedirs 加进来的，
// 而本目录下正躺着一个名字只差大小写的 ImGui.h。
// 在 Windows 上文件名不区分大小写，一旦写成 #include "imgui.h"，
// 会先命中自己这个文件，报出一堆莫名其妙的错
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <stdexcept>

#include "Platform/Window.h"
#include "Renderer/RenderCommand.h"

ImGuiLayer::ImGuiLayer(Window& window)
	: m_Window(window)
{
	// 必须放在 GL 上下文就绪之后：OpenGL3 后端会自己加载函数指针，
	// 并通过 glGetString 确认上下文版本
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

	// install_callbacks = true：ImGui 接管 GLFW 的输入回调。
	// 它会先把 Window 已经装好的回调存下来，再在自己的回调里转调，
	// 所以 ESC 退出、滚轮缩放、光标移动一个都不会被吞掉
	if (!ImGui_ImplGlfw_InitForOpenGL(m_Window.GetNativeHandle(), true))
	{
		// 构造函数抛异常时析构函数不会执行，已经建好的上下文要自己清掉
		ImGui::DestroyContext();
		throw std::runtime_error("[ImGuiLayer] ImGui_ImplGlfw_InitForOpenGL failed");
	}

	// "#version 330" 必须与工程里 GLSL 的版本一致
	if (!ImGui_ImplOpenGL3_Init("#version 330"))
	{
		// 同理：把前面已经成功的那一步也回滚掉
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		throw std::runtime_error("[ImGuiLayer] ImGui_ImplOpenGL3_Init failed");
	}
}

ImGuiLayer::~ImGuiLayer()
{
	// 顺序与初始化时相反，且必须在 GL 上下文销毁之前执行。
	// 这就是"ImGuiLayer 要声明在 Window 之后"的原因
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void ImGuiLayer::BeginFrame()
{
	// 三个调用缺一不可，顺序也不能换
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void ImGuiLayer::EndFrame()
{
	// 生成 draw data：必须在所有控件构建完之后调用
	ImGui::Render();

	// 视口要恢复成窗口尺寸：渲染场景时被改成过 FBO 的尺寸，
	// 不恢复的话 ImGui 会画错位置（或者只占画面一角）
	RenderCommand::SetViewport(0, 0,
	                           static_cast<int32_t>(m_Window.GetWidth()),
	                           static_cast<int32_t>(m_Window.GetHeight()));
	RenderCommand::Clear();

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
