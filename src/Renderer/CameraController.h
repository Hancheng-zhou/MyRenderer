#pragma once

#include "Renderer/Camera.h"

// ===========================================================================
//  ZHC - Renderer/CameraController.h
//
//  "输入 → 相机动作"的全部逻辑，连它自己的状态一起收在这里。
//
//  依赖边界（这是本文件最重要的约定）：
//    本文件【不包含任何窗口 / GLFW / ImGui 头】。
//    它只消费 InputState —— 一个纯数据的每帧输入快照。
//
//    按键映射（"W 是 +up、D 是 +right"）属于【绑定】逻辑，归输入层
//    （main 里的 PollInput）；相机控制器只该知道"有个轴让我往上走"。
//    这样以后要加手柄 / 脚本回放 / 无头测试，只需要换一个填 InputState
//    的地方，这个类一行都不用改。
//
//    这也是 Camera.h 里那条原则的上移：
//      "不管：窗口与输入 … 换输入源（手柄/脚本/回放）时不用动它"
//
//  生命周期：
//    m_Camera 是【引用】，不拥有相机 —— 相机归 Scene 所有。
//    所以 CameraController 必须在 scene 之后声明（这样它先析构）。
// ===========================================================================

// 一帧输入的【快照】。纯数据，不含任何平台类型。
struct InputState
{
	glm::vec3 MoveAxis{ 0.0f };        // 已归一化的三轴：(前后, 上下, 左右)
	bool Fast             = false;     // Shift 加速
	bool RotateHold       = false;     // 右键按住
	bool ViewportHovered  = false;     // 鼠标是否悬停在场景视口上（用【上一帧】的值）
	bool AcceptCameraKeys = false;     // 键盘是否该归相机（ImGui 没抢走）
};

class CameraController
{
public:
	// 构造时会把相机当前状态记为"启动视角"，数字键 5 可以回到这里
	explicit CameraController(Camera& camera);

	// ---- 由窗口回调转发进来：只【记录】原始输入，不做任何决策 ----
	//
	// 决策（该不该作用于相机）必须留到 OnUpdate：回调发生在 PollEvents()
	// 期间，那一刻当帧的 ViewportHovered 还不存在 —— 它要等 UI 构建完
	// 才算得出来。所以这里只攒，不判。
	void OnCursorMove(double x, double y);
	void OnScroll(double yOffset);

	// 每帧调用一次。
	//
	// 【调用时机】必须在 UI 构建【之后】—— 因为 UI 是 ViewportHovered
	// 的生产者（鼠标可能在面板上，也可能在视口上）。
	// 不过 main 传进来的是上一帧的值，原因见 EditorUI::WasViewportHovered()
	void OnUpdate(const InputState& input, float deltaTime);

	// ---- 视角预设 ----
	// 学习渲染器最需要的是"复现同一个观察条件"：对比两个算法的差异时，
	// 如果观察角度不同，看到的差别可能全来自视角而不是算法本身。
	// 所以给 4 个标准机位 + 4 个可覆盖的槽位，一键切到固定视角
	void CaptureStartupView();          // 把当前状态记为"出厂设置"
	void SaveViewSlot(int slot);        // 覆盖某个槽位（对应 F1~F4）
	void RecallViewSlot(int slot);      // 切到某个槽位（对应 1~4）
	void ResetToStartupView();          // 回到出厂设置（对应 5）

private:
	// -----------------------------------------------------------------------
	//  状态跟着逻辑走 —— 这就是抽取的全部意义。
	//  这些字段原本散在 main 里，现在 main 里一个都不剩。
	// -----------------------------------------------------------------------

	// 交互状态
	bool m_Rotating = false;

	// 上一次光标位置。必须一直维护（不管转不转），
	// 否则"按下右键"到"第一次移动"之间的位移会被算成一次巨大的 delta
	double m_LastCursorX = 0.0;
	double m_LastCursorY = 0.0;

	// 累积量：回调只管往里加，OnUpdate 里统一消费并清空
	float m_PendingDx   = 0.0f;
	float m_PendingDy   = 0.0f;
	float m_ScrollDelta = 0.0f;

	// 手感参数与逻辑是一体的，一起收进来
	float m_MoveSpeed         = 2.5f;    // 单位 / 秒
	float m_FastFactor        = 5.0f;    // 按住 Shift 时的倍率
	float m_RotateSensitivity = 0.15f;   // 度 / 像素
	float m_ZoomSensitivity   = 2.0f;    // 度 / 滚轮格

	// 视角预设
	CameraState m_ViewSlots[4];
	CameraState m_StartupView;

	Camera& m_Camera;   // 引用：相机归 Scene，这里只是操作它
};
