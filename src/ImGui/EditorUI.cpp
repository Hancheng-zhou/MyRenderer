#include "EditorUI.h"

#include <algorithm>   // std::min / std::max
#include <cmath>       // std::acos / std::cos
#include <cstdio>      // std::snprintf

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include "Renderer/Camera.h"
#include "Renderer/FrameBuffer.h"
#include "Scene/Scene.h"

// ===========================================================================
//  这些面板原本写在 main.cpp 的匿名 namespace 里，参数列表一路膨胀到
//  (scene, textures, selectedObject, gammaCorrect, keepAspectRatio,
//   showDemoWindow, io, frameBuffer ...)。
//  搬进来之后它们改成读写成员，对外只剩一个 Draw()。
// ===========================================================================

// 相机调参面板——ImGui 对学习型渲染器最大的价值就在这里：
// 参数改完立刻看到结果，不用"改代码 -> 重编译 -> 再看"。
// near / far 尤其值得摆出来，它们直接决定深度缓冲的精度分布，
// 是 z-fighting、阴影贴图精度这些问题的第一现场。
void EditorUI::DrawCameraPanel(Camera& camera, bool& showDemoWindow)
{
	// 只在首次运行时生效：之后 ImGui 会记住用户拖到哪儿、拉多大
	ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);   // 高度 0 = 自适应内容

	if (!ImGui::Begin("Camera"))
	{
		// 窗口被折叠时 Begin 返回 false，但 End() 仍然必须配对调用
		ImGui::End();
		return;
	}

	// 先写进临时 state，只有真的改动了才回写相机，
	// 避免每帧无意义地重算 view / projection 矩阵
	CameraState state = camera.GetState();

	bool changed = false;
	changed |= ImGui::DragFloat3("Position", &state.Position.x, 0.05f);
	changed |= ImGui::SliderFloat("Yaw",   &state.Yaw,        -180.0f, 180.0f, "%.1f deg");
	changed |= ImGui::SliderFloat("Pitch", &state.Pitch,      -Camera::k_MaxPitch, Camera::k_MaxPitch, "%.1f deg");
	changed |= ImGui::SliderFloat("FOV",   &state.FovDegrees, Camera::k_MinFov, Camera::k_MaxFov, "%.1f deg");

	// near / far 跨好几个数量级（0.001 ~ 1000），线性拖拽在小值区间根本动不了
	changed |= ImGui::DragFloat("Near", &state.NearZ, 0.01f, 0.001f,  10.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
	changed |= ImGui::DragFloat("Far",  &state.FarZ,  1.0f,   1.0f,  1000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);

	if (changed)
	{
		camera.SetState(state);
	}

	ImGui::Separator();

	if (ImGui::Button("Front")) { camera.SetState(CameraState::FromLookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f))); }
	ImGui::SameLine();
	if (ImGui::Button("Right")) { camera.SetState(CameraState::FromLookAt(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f))); }
	ImGui::SameLine();
	if (ImGui::Button("Top"))   { camera.SetState(CameraState::FromLookAt(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f))); }
	ImGui::SameLine();
	if (ImGui::Button("3/4"))   { camera.SetState(CameraState::FromLookAt(glm::vec3(3.0f, 3.0f, 3.0f), glm::vec3(0.0f))); }

	ImGui::Separator();

	const ImGuiIO& io = ImGui::GetIO();
	ImGui::Text("Aspect : %.4f", camera.GetAspectRatio());
	ImGui::Text("Frame  : %.1f FPS (%.2f ms)", io.Framerate,
	            io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);

	// 接入验证用：Demo 窗口把所有控件都摆出来，
	// 只要它能正常显示交互，说明字体、输入、裁剪都通了
	ImGui::Checkbox("Show ImGui Demo", &showDemoWindow);

	ImGui::End();
}

// 场景视图：把 FBO 的颜色附件贴进 ImGui 窗口。
// 渲染分辨率和显示尺寸就此解耦——画面会被缩放显示，
// 但渲染始终发生在 FBO 的像素上，这正是后处理需要的形态。
void EditorUI::DrawScenePanel(const FrameBuffer& frameBuffer)
{
	// 每帧先清零：窗口被折叠或数据异常时不该保留上一帧的悬停状态
	m_SceneHovered = false;

	ImGui::SetNextWindowPos(ImVec2(340.0f, 20.0f), ImGuiCond_FirstUseEver);
	// 默认尺寸取 16:9，与窗口（1920x1080）一致，这样首次打开几乎没有留白
	ImGui::SetNextWindowSize(ImVec2(900.0f, 506.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Scene"))
	{
		ImGui::End();
		return;
	}

	// 信息行和开关必须放在图像【之前】：
	// 图像要吃掉剩下的全部高度，放在它后面会把内容顶出窗口，
	// 逼出滚动条——而滚动条又会压缩图像宽度，形成没必要的留白
	ImGui::Text("Render target : %u x %u", frameBuffer.GetWidth(), frameBuffer.GetHeight());
	ImGui::Checkbox("Keep aspect ratio", &m_KeepAspectRatio);

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	if (avail.x > 0.0f && avail.y > 0.0f)
	{
		ImVec2 size = avail;

		if (m_KeepAspectRatio)
		{
			// 面板比例和渲染目标比例不一致时，保持比例必然留白。
			// 这里把多出来的空间平分成两半，让留白左右（或上下）对称，
			// 看起来像"画框"，而不是画面没铺满
			const float aspect = static_cast<float>(frameBuffer.GetWidth()) /
			                     static_cast<float>(frameBuffer.GetHeight());

			if (avail.x / avail.y > aspect)
			{
				size.x = avail.y * aspect;   // 面板比画面宽：按高度撑满，左右留白
			}
			else
			{
				size.y = avail.x / aspect;   // 面板比画面高：按宽度撑满，上下留白
			}

			const ImVec2 cursor = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - size.x) * 0.5f,
			                           cursor.y + (avail.y - size.y) * 0.5f));
		}

		// 纹理 ID 转 ImTextureID（1.89 里它就是 void*）。
		// 必须过一趟 intptr_t：uint32 直接转指针在 64 位下语义不明确
		const ImTextureID textureId =
			reinterpret_cast<ImTextureID>(static_cast<intptr_t>(frameBuffer.GetColorAttachmentID()));

		// UV 的 V 轴要翻转：GL 纹理原点在左下，ImGui 的在左上，
		// 不翻的话画面是上下颠倒的
		ImGui::Image(textureId, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

		// 只在图像本身之上才算悬停：窗口的标题栏、空白区都不算，
		// 那些地方的操作该留给 ImGui
		m_SceneHovered = ImGui::IsItemHovered();
	}

	ImGui::End();
}

void EditorUI::DrawTexturePicker(std::vector<TextureEntry>& entries,
                                 Material& material,
                                 int objectIndex)
{
	ImGui::PushID(objectIndex);

	// 当前选中项：拿【指针】去比，不要比名字或路径。
	// 同一张纹理可以被多个材质共享，只有指针比较才能正确判断
	// "是不是同一张"
	const char* current = "(none)";
	for (const TextureEntry& entry : entries)
	{
		if (entry.Handle == material.AlbedoTexture)
		{
			current = entry.Name.c_str();
			break;
		}
	}

	ImGui::SetNextItemWidth(-1.0f);   // 撑满面板宽度
	if (ImGui::BeginCombo("##texture", current))
	{
		// "(none)" 是材质的【正常状态】，不是"还没选"，
		// 所以要能明确地选回去（配合 shader 里的 u_UseTex 分支）
		if (ImGui::Selectable("(none)", material.AlbedoTexture == nullptr))
		{
			material.AlbedoTexture = nullptr;
		}

		for (const TextureEntry& entry : entries)
		{
			if (!entry.Handle)
			{
				continue;
			}

			// 缩略图。UV 必须【翻转 V】，原因和 Scene 面板显示 FBO 时
			// 完全一样：这张纹理在解码时被 stb 翻转过一次
			//（为了让 OpenGL 的 uv(0,0) 落在图片左下角），
			// 而 ImGui 控件的左上角就是 uv(0,0)，不翻回来图片是倒的。
			// 所以两处用的是同一组参数，这不是巧合
			ImGui::Image(
				reinterpret_cast<ImTextureID>(
					static_cast<intptr_t>(entry.Handle->GetRendererID())),
				ImVec2(20.0f, 20.0f),
				ImVec2(0.0f, 1.0f),
				ImVec2(1.0f, 0.0f));

			ImGui::SameLine();

			const bool selected = (entry.Handle == material.AlbedoTexture);
			if (ImGui::Selectable(entry.Name.c_str(), selected))
			{
				material.AlbedoTexture = entry.Handle;
			}
		}

		ImGui::EndCombo();
	}

	ImGui::PopID();
}

// 渲染相关的开关与参数面板。
//
// 只开【一个】窗口，用下拉框切换"当前编辑哪个物体"。
//
// 为什么不能"每个物体一个面板"：
//   ImGui 用窗口名当唯一标识，两次 Begin("Renderer") 不会开出两个窗口，
//   而是往同一个窗口里【追加】内容（imgui.cpp 里明确写了这个行为）；
//   更麻烦的是同一窗口里两个同名控件（两个 "Ambient"）会算出完全相同的
//   控件 ID，被当成同一个控件 —— 拖动时互相抢占，数值到底写进谁不确定。
void EditorUI::DrawRendererPanel(Scene& scene,
                                std::vector<TextureEntry>& textures,
                                RenderSettings& settings)
{
	ImGui::SetNextWindowPos(ImVec2(20.0f, 320.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Renderer"))
	{
		ImGui::End();
		return;
	}

	ImGui::Checkbox("Gamma correct", &settings.GammaCorrect);

	if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 物体数量以后会变（运行时加物体），下标每帧都要夹紧，否则越界
		if (m_SelectedObject >= static_cast<int>(scene.GetObjectCount()))
		{
			m_SelectedObject = 0;
		}

		// BeginCombo 要的是 const char*，不能直接把 std::string 喂进去
		char label[32];
		std::snprintf(label, sizeof(label), "Object %d", m_SelectedObject);

		if (ImGui::BeginCombo("Target", label))
		{
			for (int i = 0; i < static_cast<int>(scene.GetObjectCount()); ++i)
			{
				std::snprintf(label, sizeof(label), "Object %d", i);
				const bool isSelected = (i == m_SelectedObject);

				if (ImGui::Selectable(label, isSelected))
				{
					m_SelectedObject = i;
				}
				// 键盘上下键导航时焦点要跟上，少了这句就只能用鼠标点
				if (isSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (scene.GetObjectCount() > 0)
		{
			// 每帧现取。不要跨帧存这个引用 ——
			// m_SelectedObject 一变它指向别的物体，vector 扩容时更是直接悬空
			RenderObject& object = scene.GetObject(static_cast<std::size_t>(m_SelectedObject));
			ImGui::Checkbox("Enabled", &object.IsVisible);
			ImGui::Separator();
			Material& material = scene.GetObject(static_cast<std::size_t>(m_SelectedObject)).Mat;
			
			// 拖拽速度必须显式给一个小值：DragFloat3 默认 v_speed = 1.0f，
			// 对 0~1 的颜色来说一像素就跳 1.0，根本没法微调
			//
			// 这里编辑的是【线性空间】的数值本身，色块看着会比最终画面亮一档，
			// 判断效果请以 Scene 视口为准
			ImGui::DragFloat3("Ambient",  glm::value_ptr(material.Ambient),  0.01f);
			ImGui::DragFloat3("Diffuse",  glm::value_ptr(material.Diffuse),  0.01f);
			ImGui::DragFloat3("Specular", glm::value_ptr(material.Specular), 0.01f);
			ImGui::SliderFloat("Shininess", &material.Shininess, 1.0f, 256.0f, "%.0f");

			DrawTexturePicker(textures, material, m_SelectedObject);

			// 把贴图状态摆出来：一眼看清哪张贴图挂在了哪个物体上
			ImGui::Text("Albedo : %s",
			            material.AlbedoTexture ? material.AlbedoTexture->GetPath().c_str() : "(none)");
		}
	}

	if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const bool full = scene.GetLightCount() >= Scene::k_MaxLightsCount;

		// 到上限就把按钮禁用，而不是让人点了没反应
		ImGui::BeginDisabled(full);
		if (ImGui::Button("Add Light"))
		{
			scene.AddLight(Light{});
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (full)
		{
			// 明确说出"到上限了"：能加但不能渲染，比按钮坏了更难查
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
			                   "%zu / %zu (MAX)", scene.GetLightCount(), Scene::k_MaxLightsCount);
		}
		else
		{
			ImGui::Text("%zu / %zu", scene.GetLightCount(), Scene::k_MaxLightsCount);
		}

		for (std::size_t i = 0; i < scene.GetLightCount(); ++i)
		{
			// PushID 隔离：同一窗口里多组同名控件（"Position"/"Color"...）
			// 会算出相同 ID 被当成同一个控件，拖动时互相抢占
			ImGui::PushID(static_cast<int>(i));

			char header[32];
			std::snprintf(header, sizeof(header), "Light %zu", i);

			if (ImGui::TreeNode(header))
			{
				// 每帧现取，不跨帧持有（AddLight 会扩容）
				Light& light = scene.GetLight(i);

				ImGui::Checkbox("Enabled", &light.enabled);

				int type = static_cast<int>(light.type);
				const char* k_TypeNames[] = { "Directional", "Point", "Spot" };
				if (ImGui::Combo("Type", &type, k_TypeNames, IM_ARRAYSIZE(k_TypeNames)))
				{
					light.type = static_cast<LightType>(type);
				}

				// 按类型只显示有意义的字段：
				// 平行光没有位置，点光没有方向，省得拖了没反应让人困惑
				if (light.type != LightType::Directional)
				{
					ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.05f);
				}
				if (light.type != LightType::Point)
				{
					ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.01f);
				}

				ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
				ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 20.0f);

				if (light.type != LightType::Directional)
				{
					ImGui::DragFloat("Constant",  &light.constant,  0.01f,  0.0f, 10.0f);
					ImGui::DragFloat("Linear",    &light.linear,    0.001f, 0.0f,  1.0f);
					ImGui::DragFloat("Quadratic", &light.quadratic, 0.001f, 0.0f,  1.0f);
				}

				if (light.type == LightType::Spot)
				{
					// 拖的是角度（度），存进去的是 cos —— 直接拖 cos 值
					// 在 0.95~1.0 之间几乎拖不动
					float innerDeg = glm::degrees(std::acos(light.innerCutoff));
					float outerDeg = glm::degrees(std::acos(light.outerCutoff));

					if (ImGui::SliderFloat("Inner Angle", &innerDeg, 1.0f, 60.0f, "%.1f deg"))
					{
						// 内角必须严格小于外角：反了的话两个 cos 的大小关系颠倒，
						// 就成了 smoothstep(edge0 > edge1) —— GLSL 规范里明确
						// 这是【未定义行为】，不同驱动给的结果可能完全不同
						innerDeg = std::min(innerDeg, outerDeg - 0.5f);
						light.innerCutoff = glm::cos(glm::radians(innerDeg));
					}
					if (ImGui::SliderFloat("Outer Angle", &outerDeg, 1.0f, 60.0f, "%.1f deg"))
					{
						outerDeg = std::max(outerDeg, innerDeg + 0.5f);
						light.outerCutoff = glm::cos(glm::radians(outerDeg));
					}
				}

				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}

	ImGui::End();
}

void EditorUI::Draw(Scene& scene,
                    std::vector<TextureEntry>& textures,
                    const FrameBuffer& frameBuffer,
                    RenderSettings& settings)
{
	DrawScenePanel(frameBuffer);
	DrawCameraPanel(scene.GetCamera(), m_ShowDemoWindow);
	DrawRendererPanel(scene, textures, settings);

	if (m_ShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&m_ShowDemoWindow);
	}
}
