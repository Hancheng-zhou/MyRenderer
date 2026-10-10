#include "EditorUI.h"

#include <algorithm>   // std::min / std::max
#include <cmath>       // std::acos / std::cos
#include <cstdint>     // uint32_t
#include <cstdio>      // std::snprintf

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include "Core/PrimitiveDesc.h"   // PrimitiveType / k_PrimitiveTypeNames
#include "Renderer/Camera.h"
#include "Renderer/FrameBuffer.h"
#include "Renderer/Mesh.h"        
#include "Scene/Scene.h"
#include "Renderer/PostProcess.h"

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

// 一个 uint32_t 的滑块。
//
// 【为什么需要这个小工具】ImGui::SliderInt 只接受 int*，而几何的细分
// 数量是 uint32_t。不要用 reinterpret_cast<int*> 硬转过去：
// 那违反严格别名规则 —— 能跑，但不保证正确，换个优化等级就可能出错。
// 走一次显式中转，代价只是两个寄存器
static void SliderCount(const char* label, uint32_t& value, int minValue, int maxValue)
{
	int temp = static_cast<int>(value);
	if (ImGui::SliderInt(label, &temp, minValue, maxValue))
	{
		value = static_cast<uint32_t>(temp);
	}
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
	const FrameBuffer& shadowMap,const FrameBuffer& bloomMap, RenderSettings& settings)
{
	ImGui::SetNextWindowPos(ImVec2(20.0f, 320.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Renderer"))
	{
		ImGui::End();
		return;
	}

	// -----------------------------------------------------------------------
	//  1. 物体编辑：选物体 → 它的全部属性
	//
	//  结构：一个下拉框选物体 → 下面全是【这个物体的】全部属性。
	//
	//  "改哪个物体"和"改它的什么"是两个独立的问题：前者由下拉框回答一次，
	//  后者按 Geometry / Transform / Material 分组，正好对应 RenderObject
	//  的成员划分。以后加新属性只要多一个折叠头，不用再纠结"这段挂在哪"
	// -----------------------------------------------------------------------
	if (scene.GetObjectCount() == 0)
	{
		// 明确说"场景是空的"，而不是让下拉框和内容静默消失 ——
		// 后者会让人以为面板坏了
		ImGui::TextDisabled("(scene has no objects)");
	}
	else
	{
		// 物体数量以后会变（运行时加物体），下标每帧都要夹紧，否则越界
		if (m_SelectedObject >= static_cast<int>(scene.GetObjectCount()))
		{
			m_SelectedObject = 0;
		}

		// ---- 选物体 ----
		// BeginCombo 要的是 const char*，不能直接把 std::string 喂进去，所以现拼缓冲。
		//
		// 【显示 Name，而不是 "Object 0"】
		// 场景里三样东西都叫 "Object N" 时，你没法知道自己在改哪一个 ——
		// 而三个物体常常共享同一份 Mesh，重建只影响被选中的那个。
		// "改了参数画面不动"很大一部分就是这么来的。
		// Name 为空就退回编号：手搭场景时很容易忘了设名字，
		// 那种情况下显示一行空白比显示编号更糟
		auto makeObjectLabel = [](char* buffer, std::size_t capacity,
		                          int index, const RenderObject& item)
		{
			if (item.Name.empty())
			{
				// 把"不可见"标在名字上：一眼看出哪个被关掉了，
				// 否则关了之后只能一个个点开找
				std::snprintf(buffer, capacity, "Object %d%s",
				              index, item.IsVisible ? "" : "  (hidden)");
			}
			else
			{
				std::snprintf(buffer, capacity, "%s%s",
				              item.Name.c_str(), item.IsVisible ? "" : "  (hidden)");
			}
		};

		// 预览文字和列表项各用一块缓冲：共用一块虽然也能跑，
		// 但一旦 ImGui 哪天改成延迟使用这个指针，就会读到循环最后一次的内容
		char previewLabel[64];
		makeObjectLabel(previewLabel, sizeof(previewLabel), m_SelectedObject,
		                scene.GetObject(static_cast<std::size_t>(m_SelectedObject)));

		if (ImGui::BeginCombo("Target", previewLabel))
		{
			for (int i = 0; i < static_cast<int>(scene.GetObjectCount()); ++i)
			{
				const RenderObject& item = scene.GetObject(static_cast<std::size_t>(i));

				char itemLabel[64];
				makeObjectLabel(itemLabel, sizeof(itemLabel), i, item);

				const bool isSelected = (i == m_SelectedObject);

				if (ImGui::Selectable(itemLabel, isSelected))
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

		// 每帧现取。不要跨帧存这个引用 ——
		// m_SelectedObject 一变它指向别的物体，vector 扩容时更是直接悬空
		RenderObject& object = scene.GetObject(static_cast<std::size_t>(m_SelectedObject));

		ImGui::Checkbox("Visible", &object.IsVisible);

		// -------------------------------------------------------------------
		//  几何：物体的"形状"
		//
		//  结构：先选"生成什么类型"，再给该类型有意义的参数，最后一步"应用"。
		//  所有几何共用同一条流程，加形状不用动面板结构
		// -------------------------------------------------------------------
		if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// ---- 只读信息 ----
			//
			// 没有几何的物体会被 SceneRenderer 【静默跳过】：
			// 画面里什么都没有，也不报错。而"忘了写 object.Geometry = mesh;"
			// 是极容易犯的错，所以这里用红字明确标出来
			if (!object.Geometry)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
				                   "No geometry - this object is not rendered");
			}
			else
			{
				ImGui::Text("Vertices : %u", object.Geometry->GetVertexCount());
				ImGui::Text("Indices  : %u", object.Geometry->GetIndexCount());
			}

			// ---- 配方：生成什么 ----
			//
			// 改这里只改【数据】，不碰显存；真的重建由下面的按钮触发
			int type = static_cast<int>(object.Primitive.Type);
			if (ImGui::Combo("Type", &type, k_PrimitiveTypeNames, k_PrimitiveTypeCount))
			{
				object.Primitive.Type = static_cast<PrimitiveType>(type);
			}

			// ---- 配方：按什么参数生成 ----
			//
			// 按类型只显示有意义的参数。和 Light 面板里"平行光不显示 Position"
			// 是同一个做法：拖了没有任何反应的控件，比不显示更让人困惑
			if (object.Primitive.Type == PrimitiveType::Sphere)
			{
				ImGui::DragFloat("Radius", &object.Primitive.Size, 0.02f, 0.05f, 5.0f, "%.2f");

				// 细分级别用滑块而不是拖数字：范围小、上下限明确，
				// "往低拖会变成多面体"这件事能直接看见
				SliderCount("Segments", object.Primitive.Segments, 3, 128);
				SliderCount("Rings",    object.Primitive.Rings,    2,  64);
			}
			else
			{
				ImGui::DragFloat("Cube size", &object.Primitive.Size, 0.02f, 0.05f, 5.0f, "%.2f");
			}

			// ---- 配方 → 几何：刻意拆成【两步】----
			//
			//   改参数 = 只改 CPU 侧的数据，不碰显存
			//   点按钮 = 真的重建 Mesh（新建 VAO / VBO / EBO）
			//
			// 【为什么要拆开】原来的写法是"拖一下重建一次"，于是按钮永远
			// 只能重复一遍拖动已经做过的事，结果完全相同 ——
			// 看上去就是"Rebuild 无效"。拆开之后按钮有了唯一且明确的语义：
			// 把当前这份配方应用到几何上。
			//
			// 【为什么不让改参数就直接重建】Mesh 的构造是一整套 GL 资源分配 +
			// 显存上传，拖动过程中每帧执行等于每帧重新分配一次，
			// 旧的那份还要等 shared_ptr 归零才释放。
			// 而且"改过但还没应用"这个中间状态本身是有用的信息 ——
			// 面板能把它显示出来，你才知道眼前这个几何对应的是哪份配方
			//
			// 判断收在 RenderObject::IsGeometryPending() 里，面板不自己拼条件：
			// 否则"什么算待重建"会有两份定义，早晚不一致
			const bool pending = object.IsGeometryPending();

			// 未应用时把按钮染成醒目色 —— 否则"改了参数忘了点"是完全看不出来的
			if (pending)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
			}
			const bool clicked = ImGui::Button("Rebuild geometry");
			if (pending)
			{
				ImGui::PopStyleColor();
			}

			if (clicked)
			{
				// 重建几何 + 记账（BuiltPrimitive）都在这个函数里，
				// 调用方不可能只做一半 —— 只换 Geometry 不记账的话，
				// 面板会永远显示"已同步"，你再也看不到"待应用"
				object.RebuildGeometry();
			}

			ImGui::SameLine();
			if (pending)
			{
				// 把"几何现在是什么"摊开说，一眼就知道差在哪
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
				                   "pending (mesh: %s %.2f)",
				                   PrimitiveTypeName(object.BuiltPrimitive.Type),
				                   object.BuiltPrimitive.Size);
			}
			else
			{
				ImGui::TextDisabled("up to date");
			}
		}

		// -------------------------------------------------------------------
		//  变换：物体在哪儿、多大、朝哪
		// -------------------------------------------------------------------
		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::DragFloat3("Position", glm::value_ptr(object.Xform.Position), 0.05f);

			// Rotation 在数据层存的是【弧度】（glm::eulerAngleXYZ 吃弧度），
			// 但让人拖一个 -3.14 ~ 3.14 的数值很难用，所以这里转成"度"再显示。
			//
			// 【注意】main 里的 UpdateSceneAnimation 每帧都会覆盖 Rotation.y
			//（那个自转动画），所以拖 Y 看不出变化 —— 那不是 bug。
			// 想手动摆姿势就拖 X / Z，或者暂时注释掉那个函数里的 Rotation.y
			glm::vec3 rotationDegrees = glm::degrees(object.Xform.Rotation);
			if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotationDegrees),
			                      1.0f, -180.0f, 180.0f, "%.1f deg"))
			{
				object.Xform.Rotation = glm::radians(rotationDegrees);
			}

			// Scale 刻意允许非均匀 —— 顺便验证 GetNormalMatrix()：
			// 法线矩阵写错的话，拉长之后光照会整个歪掉
			ImGui::DragFloat3("Scale", glm::value_ptr(object.Xform.Scale), 0.05f, 0.01f, 20.0f);
		}

		// -------------------------------------------------------------------
		//  材质：物体的"外观"
		// -------------------------------------------------------------------
		if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			Material& material = object.Mat;
			
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

	// -----------------------------------------------------------------------
	//  2. 光源
	//
	//  光源【不属于】任何 RenderObject —— 它照的是整个场景，
	//  所以单独一节放在物体编辑之后，而不是塞进上面那个下拉框里
	// -----------------------------------------------------------------------
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
				ImGui::Checkbox("Shadowcast", &light.castShadow);
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

	// -----------------------------------------------------------------------
	//  3. 阴影
	//
	//  放在物体/光源【之后】有实际理由，不只是审美：
	//  它调的是"整帧怎么算阴影"，和被编辑的物体无关，
	//  而 Extent / Distance 的意义要对照上面那盏灯的 direction 才看得懂 ——
	//  先看到灯、再调阴影，顺序和思考顺序一致
	//
	//  bias 没有公式（它兜的是数值误差），但软阴影的两个参数有 ——
	//  它们都挂在"一个纹素的世界尺寸"上，所以下面把那个基准值直接显示出来，
	//  调参时不用心算
	// -----------------------------------------------------------------------
	if (ImGui::CollapsingHeader("Shadow", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Enabled", &settings.ShadowEnabled);

		// 开了法线偏移之后，bias 只负责兜住极端掠射角，所以上限收到 0.005
		ImGui::SliderFloat("Bias", &settings.ShadowBias, 0.0f, 0.005f, "%.4f");

		//// 硬边 / 软边对比开关。shader 里判的是"半径 <= 0 就当硬边"，
		//// 用一个复选框比让人理解"半径 = 0 反而表示硬边"直观
		//bool hardEdge = (settings.ShadowPCFRadius <= 0);
		//if (ImGui::Checkbox("Hard edge", &hardEdge))
		//{
		//	settings.ShadowPCFRadius = hardEdge ? 0 : 1;
		//}

		// 过滤半径的【上限】，单位【纹素】。0 = 单次采样的硬边（拿来做 A/B 对比）。
		//
		// 【它不等于 PCSS 的实际半径】shader 里每个像素的半径是由
		// "遮挡物离接收面多远"算出来的，这个值只是给它封顶。
		// 上限定太小（比如 1）会把 PCSS 完全压平 —— 所有地方糊得一样多，
		// 接触硬化就看不出来了
		ImGui::SliderInt("Filter radius cap", &settings.FilterRadius, 0, 64);

		// PCSS 的软度比例（"光源视半径"）。决定"影子能软到什么程度"：
		// 调大 -> 半影显著变宽；调到接近 0 -> 退化成接近硬边的 PCF。
		// 用对数刻度：0.005 ~ 0.5 跨两个数量级，线性滑块在小值区间根本拖不动
		ImGui::SliderFloat("Light size", &settings.ShadowLightSize,
		                   0.005f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);

		// 取景范围：太小 -> 场景有部分落在视锥外、那部分不出阴影；
		// 太大 -> 深度精度被摊薄，acne 变严重
		ImGui::DragFloat("Extent",   &settings.ShadowExtent,   0.5f, 1.0f, 64.0f);
		ImGui::DragFloat("Distance", &settings.ShadowDistance, 0.5f, 1.0f, 64.0f);


		// ---- 把 shadow map 贴出来 ----
		//
		// 这是整个功能里最值钱的调试手段：深度值都在 [0,1]，
		// 直接当灰度图看就很有信息量 ——
		//   白 = 那个方向没撞到东西（远平面）
		//   黑 = 离光源近的表面
		// 一眼就能看出光照相机摆对没有、场景有没有落在视锥里。
		//
		// 没有它的话，"影子不对"和"深度图不对"这两种病你分不开
		const float previewSize = ImGui::GetContentRegionAvail().x;
		ImGui::Image(
			reinterpret_cast<ImTextureID>(
				static_cast<intptr_t>(shadowMap.GetDepthAttachmentID())),
			ImVec2(previewSize, previewSize),
			ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));   // V 轴同样要翻
	}

	// -----------------------------------------------------------------------
	//  4. 输出 / 后处理
	//
	//  后处理是"整帧最后怎么变成能看的照片"，跟物体、灯、阴影都没关系，
	//  放进任何一节都不合适，所以单独收在末尾
	// -----------------------------------------------------------------------
	ImGui::Checkbox("Post process", &settings.PostProcessEnabled);
	if (settings.PostProcessEnabled)
	{
		ImGui::SliderFloat("Exposure", &settings.Exposure, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		ImGui::Combo("Tone mapping", &settings.TonemapMode, "Off\0Reinhard\0ACES\0");

		bool debugView = (settings.DebugHDRView != 0);
		if (ImGui::Checkbox("Debug: HDR stops", &debugView))
		{
			settings.DebugHDRView = debugView ? 1 : 0;
		}
		ImGui::Separator();
		ImGui::Checkbox("Bloom", &settings.BloomEnabled);
		if (settings.BloomEnabled)
		{
			ImGui::SliderFloat("Threshold", &settings.BloomThreshold,0.0f,5.0f,"%.2f");
			ImGui::SliderFloat("Knee", &settings.BloomKnee,0.0f,2.0f,"%.2f");
			ImGui::SliderFloat("Strength", &settings.BloomStrength, 0.0f, 2.0f, "%.2f");
			ImGui::SliderFloat("Radius", &settings.BloomRadius, 0.5f, 3.0f, "%.2f");
			ImGui::SliderFloat("Scatter", &settings.BloomScatter, 0.0f, 1.0f, "%.2f");
			ImGui::SliderInt("Levels", &settings.BloomLevels,
				1, PostProcess::k_MaxLevel);
			const float previewW = ImGui::GetContentRegionAvail().x;
			ImGui::Image(
				reinterpret_cast<ImTextureID>(
					static_cast<intptr_t>(bloomMap.GetColorAttachmentID())),
				ImVec2(previewW, previewW * 0.5f),
				ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
		}


	}

	ImGui::End();
}

void EditorUI::Draw(Scene& scene,
	std::vector<TextureEntry>& textures,
	const FrameBuffer& frameBuffer,
	const FrameBuffer& shadowMap, const FrameBuffer& bloomMap, RenderSettings& settings)
{
	DrawScenePanel(frameBuffer);
	DrawCameraPanel(scene.GetCamera(), m_ShowDemoWindow);
	DrawRendererPanel(scene, textures, shadowMap, bloomMap, settings);

	if (m_ShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&m_ShowDemoWindow);
	}
}
