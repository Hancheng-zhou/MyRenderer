#pragma once

class Window;

// ===========================================================================
//  ZHC - ImGui/ImGui.h
//
//  ImGui 生命周期的封装。
//
//  为什么要单独抽出来：
//    main 里真正属于"应用流程"的只有三句 —— 开一帧、构建 UI、结束一帧。
//    而 CreateContext / 两个后端 Init / NewFrame / Render / Shutdown
//    这些纯事务性的代码有十几行，留在 main 里会淹没真正的逻辑。
//
//  边界：
//    管：ImGui 上下文的建立与销毁、每帧的帧开始与提交
//    不管：具体面板长什么样。那些依赖 Camera / FrameBuffer 等应用状态，
//          属于应用层，留在 main 里
//
//  类名说明：
//    不能叫 ImGui —— 那会和 imgui.h 里的 namespace ImGui 正面冲突
//
//  生命周期（和 Window 的规则一致）：
//    析构时会调用 ImGui 的 Shutdown，而它需要当前 GL 上下文，
//    所以在 main 里必须声明在 Window【之后】（这样它先于 Window 析构）
// ===========================================================================
class ImGuiLayer
{
public:
	// 构造即完成初始化。
	// 必须晚于 Window 构造：ImGui 的平台后端会保存 Window 已装好的 GLFW 回调，
	// 并在自己的回调里转调。顺序反了的话，Window 的跳板会把 ImGui 的覆盖掉，
	// 结果是 ImGui 再也收不到输入 —— 而且不报任何错
	explicit ImGuiLayer(Window& window);
	~ImGuiLayer();

	// 持有 ImGui 上下文，禁止拷贝
	ImGuiLayer(const ImGuiLayer&) = delete;
	ImGuiLayer& operator=(const ImGuiLayer&) = delete;

	// 帧首调用：开始新的一帧，之后才能调用任何 ImGui:: 函数。
	// 另外 io.WantCaptureKeyboard / WantCaptureMouse 也只在这之后才算得出来，
	// 所以它必须排在输入处理之前
	void BeginFrame();

	// 帧尾调用：生成 draw data 并提交到默认帧缓冲（屏幕）。
	// 内部会依次做 ImGui::Render()、把视口恢复成窗口尺寸、清屏、RenderDrawData
	void EndFrame();

private:
	Window& m_Window;
};
