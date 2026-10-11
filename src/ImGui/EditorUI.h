#pragma once

#include <vector>

#include "Renderer/RenderSettings.h"
#include "Renderer/TextureLibrary.h"

class Scene;
class FrameBuffer;
class Camera;
struct Material;   // 注意是 struct 不是 class：写错会触发 C4099（前后不一致）

// ===========================================================================
//  ZHC - ImGui/EditorUI.h
//
//  编辑器面板集合：面板本身 + 面板的【状态】。
//
//  为什么要收状态：
//    m_KeepAspectRatio / m_ShowDemoWindow / m_SelectedObject 这些原本散在
//    main 里，但它们只被面板读写，对主循环毫无意义 —— 留在 main 里
//    等于让主循环替 UI 保管私人物品。
//    （注意 gammaCorrect 不在其中：它是【渲染设置】，归属见 RenderSettings.h）
//
//  与 ImGuiLayer 的分工：
//    ImGuiLayer   管 ImGui 上下文的生死、每帧的"开始 / 提交"（纯事务）
//    EditorUI     管"有哪些面板、长什么样、状态存在哪"（应用层）
//
//  调用顺序约束：
//    必须在场景渲染【之后】调用 —— Scene 面板要显示本帧的画面。
//    反过来的话拿到的是上一帧的图，拖相机会有拖影感
// ===========================================================================
class EditorUI
{
public:
	// 【sceneTextureId 由调用方决定，而不是从 frameBuffer 里取】
	//
	// 正常情况它就是 frameBuffer 的颜色附件；调试图层打开时，它可以是
	// 深度附件、bloom 某一级、或者别的缓冲 —— 面板只负责"把这张纹理
	// 贴出来"，"这张纹理是哪来的"完全由 main 决定。
	// 于是每加一个调试图层，EditorUI 一行都不用改
	void Draw(Scene& scene,
	          std::vector<TextureEntry>& textures,
	          const FrameBuffer& frameBuffer,
	          uint32_t sceneTextureId,
	          const FrameBuffer& shadowMap, const FrameBuffer& bloomMap, RenderSettings& settings);

	// 鼠标是否悬停在场景视口上。
	//
	// 【注意：它返回的是上一帧的结果】
	// 当帧的值要等面板构建完（ImGui::IsItemHovered）才知道，而输入处理
	// 排在面板之前。所以调用方拿到的是上一帧的值 ——
	// 但鼠标不可能在一帧之内跨越窗口，这个延迟看不出来，
	// 换来的是操作能当帧生效，而不是延迟一整帧才反应。
	//
	// 它是"输入该给谁"的判据：悬停在视口上时键盘/滚轮归相机，
	// 悬停在面板上时归 ImGui
	bool WasViewportHovered() const { return m_SceneHovered; }

private:
	void DrawCameraPanel(Camera& camera, bool& showDemoWindow);
	void DrawScenePanel(const FrameBuffer& frameBuffer, uint32_t textureId, int debugView);
	void DrawTexturePicker(std::vector<TextureEntry>& entries,
	                       std::shared_ptr<Texture>& current, 
						   const char* label,
	                       int objectIndex);
	void DrawRendererPanel(Scene& scene,
	                       std::vector<TextureEntry>& textures,
	                       const FrameBuffer& shadowMap,const FrameBuffer& bloomMap, RenderSettings& settings);

	// ---- 面板自己的状态 ----
	bool m_SceneHovered    = false;
	bool m_KeepAspectRatio = true;
	bool m_ShowDemoWindow  = false;
	int  m_SelectedObject  = 0;
};
