// ===========================================================================
//  ZHC - main.cpp
//
//  main 的职责只有两件事：【装配】+ 主循环骨架。
//
//    - 装配：谁先谁后、生命周期规则（有 GL 依赖的对象必须晚于 Window 构造）
//    - 主循环骨架：一帧里"先做什么、后做什么"
//
//  它【不】负责：
//    - 相机怎么动        -> CameraController
//    - 场景怎么画        -> SceneRenderer
//    - 面板长什么样      -> EditorUI
//    - 按键怎么变成轴    -> PollInput（下面）
//
//  最后这条是刻意的边界：按键映射（"W 是 +up"）属于【绑定】逻辑，归输入层；
//  CameraController 只消费 InputState，完全不认识 GLFW_KEY_*。
//  这样以后要加手柄 / 脚本回放 / 无头测试，只需要再写一个填 InputState
//  的函数，相机那套逻辑一行都不用改。
// ===========================================================================

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>   // std::move
#include <vector>

#include <glm/glm.hpp>

#include <imgui.h>

#include "ImGui/EditorUI.h"
#include "ImGui/ImGui.h"
#include "Platform/Window.h"
#include "Renderer/Camera.h"
#include "Renderer/CameraController.h"
#include "Renderer/FrameBuffer.h"
#include "Renderer/Mesh.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RenderSettings.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Shader.h"
#include "Renderer/TextureLibrary.h"
#include "Scene/Scene.h"
#include "Renderer/PostProcess.h"
namespace
{
	// -----------------------------------------------------------------------
	//  把窗口与 ImGui 的原始状态翻译成一帧的输入快照。
	//
	//  GLFW 的按键常量只在这里出现 —— 这是刻意的：
	//  CameraController 拿到的是"有个轴让我往上走"，而不是"W 被按下了"。
	// -----------------------------------------------------------------------
	InputState PollInput(Window& window, const ImGuiIO& io, bool viewportHovered)
	{
		InputState input;
		input.ViewportHovered = viewportHovered;

		// 键盘归谁：
		//   正在输入文字        -> 永远归输入框（优先级最高）
		//   光标在 Scene 视口上 -> 归相机，这样悬在视口上就能直接 WASD
		//   其余                -> 看 ImGui 是否想独占
		input.AcceptCameraKeys = !io.WantTextInput &&
		                         (viewportHovered || !io.WantCaptureKeyboard);

		if (input.AcceptCameraKeys)
		{
			// 三轴顺序 (前后, 上下, 左右) —— 与 Camera::MoveForward/MoveUp/MoveRight 对应
			if (window.IsKeyPressed(GLFW_KEY_W))            { input.MoveAxis.z += 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_S))            { input.MoveAxis.z -= 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_D))            { input.MoveAxis.x += 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_A))            { input.MoveAxis.x -= 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_SPACE))        { input.MoveAxis.y += 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) { input.MoveAxis.y -= 1.0f; }
		}

		input.Fast       = window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
		                   window.IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
		input.RotateHold = window.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);

		return input;
	}

	// -----------------------------------------------------------------------
	//  场景动画：把"这一帧世界变成什么样"写回数据。
	//
	//  刻意【不】放进 SceneRenderer：渲染阶段应该是只读的。
	//  一旦渲染时改数据，将来做阴影贴图（一帧要把场景渲染两遍）就会出问题
	// -----------------------------------------------------------------------
	void UpdateSceneAnimation(Scene& scene, double time)
	{
		const float t = static_cast<float>(time);

		for (std::size_t i = 0; i < scene.GetObjectCount(); ++i)
		{
			// Transform::Rotation 的单位是【弧度】（glm::eulerAngleXYZ 吃弧度），
			// 而 Camera 的角度用【度】—— 两套单位别混
			//
			// 用下标做相位偏移，避免所有物体转得一模一样
			//（同步旋转看起来像静止，反而看不出在动）
			/*scene.GetObject(i).Xform.Rotation.y = t * 0.6f + static_cast<float>(i) * 0.7f;*/
		}
	}
}

int main()
{
	try
	{
		// ===================================================================
		//  装配
		// ===================================================================

		// 规则 1：Window 必须是第一个声明的对象。
		// 它最后析构，保证 glfwTerminate() 在所有 GL 资源被删除之后才执行
		Window window({ "ZHC", 1920, 1080 });

		// 规则 2：构造完成 = GLFW 已初始化 + 当前线程已有当前上下文。
		// glad 的加载必须排在这之后，否则函数指针全是空
		if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
		{
			throw std::runtime_error("[GLad] gladLoadGLLoader failed");
		}

		std::printf("[GL] version  : %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
		std::printf("[GL] renderer : %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

		RenderCommand::Init();
		// 规则 3：resize 回调只在"变化"时触发，初始视口得自己设。
		// GetWidth/GetHeight 返回的是 framebuffer 尺寸（像素），正是视口要的值
		RenderCommand::SetViewport(0, 0,
		                           static_cast<int32_t>(window.GetWidth()),
		                           static_cast<int32_t>(window.GetHeight()));

		// 清屏色是 GL 状态而不是每帧的命令，设一次就够
		//
		// 【这个值必须按"线性"来填】它不再是"屏幕上显示的那个颜色"，
		// 而是"背景的辐射亮度" —— 它进的是 sceneBuffer（线性空间），
		// 之后要被曝光、色调映射、gamma 一层层处理过去。
		// 想让屏幕上呈现 0.09 的灰，反算回线性就是 pow(0.09, 2.2) ≈ 0.005
		RenderCommand::SetClearColor(glm::vec4(0.005f, 0.005f, 0.008f, 1.0f));

		// 规则 4：ImGuiLayer 必须晚于 Window ——
		// 它的平台后端会接管 Window 已经装好的回调，顺序反了 ImGui 就收不到输入
		ImGuiLayer imGuiLayer(window);
		ImGuiIO& io = ImGui::GetIO();

		// ---- 离屏渲染目标 ----
		// 场景先渲染到 FBO，再由 ImGui 当成纹理显示出来。
		// 这一步不只是"换个地方画"：一旦画面经过 FBO，就能在它与屏幕之间
		// 插入后处理（bloom / 色调映射 / SSAO ...）
		// 【shadow map 必须是正方形】
		// 正交盒是 2*Extent × 2*Extent 的正方形（见 SceneRenderer::
		// ComputeLightSpaceMatrix），贴图也得是正方形 —— 否则 x / y 方向
		// 一个纹素覆盖的世界尺寸不相等，软阴影的"圆盘"投到世界上会变成椭圆，
		// 半影在竖直方向被拉长，锯齿纹理也是各向异性的。
		// 之前是 1920x1080 配 8x8 的正交盒，两个方向差了 1.78 倍
		FrameBufferSpec shadowSpec;
		shadowSpec.Width = 2048;
		shadowSpec.Height = 2048;
		shadowSpec.DepthOnly = true;
		FrameBuffer shadowMap(shadowSpec);

		FrameBufferSpec sceneSpec;
		sceneSpec.Width = window.GetWidth();
		sceneSpec.Height = window.GetHeight();
		sceneSpec.DepthOnly = false;
		sceneSpec.ColorFormat = TextureFormat::RGBA16F;
		FrameBuffer sceneBuffer(sceneSpec);

		FrameBufferSpec postProcessSpec;
		postProcessSpec.Width = window.GetWidth();
		postProcessSpec.Height = window.GetHeight();
		postProcessSpec.DepthOnly = false;
		FrameBuffer postProcessBuffer(postProcessSpec);
		PostProcess postProcess(window.GetWidth(), window.GetHeight());

		// ---- 场景：相机、物体、光源都收在这里 ----
		// 宽高比取自 FBO 而不是窗口：真正决定画面比例的是渲染目标的尺寸
		const float initialAspect = sceneBuffer.GetHeight() > 0
			? static_cast<float>(sceneBuffer.GetWidth()) / static_cast<float>(sceneBuffer.GetHeight())
			: 16.0f / 9.0f;

		Scene scene(glm::vec3(0.0f, 0.0f, 3.0f),   // 相机位置：Z 轴正方向 3 处
		            glm::vec3(0.0f, 0.0f, 0.0f),   // 看向世界原点
		            45.0f,                          // 垂直视场角（度）
		            initialAspect,
		            0.1f,                           // near
		            100.0f);                        // far

		// CameraController 持有相机的【引用】，所以必须声明在 scene 之后
		//（这样它先析构，不会留下悬空引用）
		CameraController cameraController(scene.GetCamera());

		SceneRenderer sceneRenderer;
		EditorUI      editor;
		RenderSettings renderSettings;

		// ---- 事件注册：一律只做转发，逻辑在各对象里 ----
		window.SetCursorPosCallback([&cameraController](double x, double y)
			{
				cameraController.OnCursorMove(x, y);
			});
		window.SetScrollCallback([&cameraController](double /*xOffset*/, double yOffset)
			{
				cameraController.OnScroll(yOffset);
			});
		// 【两个 FBO 都要捕获，都要 Resize】
		// 只改 sceneBuffer 的话：窗口拉大之后 postProcessBuffer 还是旧尺寸，
		// 后处理 pass 按旧尺寸渲染（视口取自 postProcessBuffer），
		// 最终画面的分辨率停在改窗口前那一刻。症状是"糊"而不是"错位"，
		// 很容易被当成"窗口缩放本来就这样"
		window.SetResizeCallback([&scene, &sceneBuffer, &postProcessBuffer,&postProcess](uint32_t width, uint32_t height)
			{
				// 回调参数已经是 framebuffer 尺寸（像素）。
				// FBO 尺寸和相机宽高比必须一起更新，少改一个画面就会拉伸
				sceneBuffer.Resize(width, height);
				postProcessBuffer.Resize(width, height);
				postProcess.Resize(width, height);
				// 这里读 FBO 的尺寸而不是回调参数：窗口最小化时 FBO 会拒绝变成 0，
				// 相机的宽高比也该跟着保持原样
				scene.GetCamera().SetViewportSize(sceneBuffer.GetWidth(), sceneBuffer.GetHeight());
			});
		window.SetKeyCallback([&window, &cameraController](int key, int action)
			{
				// 只响应"按下"的那一瞬间：按住不放产生的 GLFW_REPEAT
				// 会让视角反复跳，GLFW_RELEASE 也不该触发
				if (action != GLFW_PRESS)
				{
					return;
				}

				if (key == GLFW_KEY_ESCAPE)
				{
					window.SetShouldClose(true);
					return;
				}

				// F1~F4：把当前视角存进对应槽位，会覆盖掉内置机位
				if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F4)
				{
					const int slot = key - GLFW_KEY_F1;
					cameraController.SaveViewSlot(slot);
					std::printf("[Camera] saved current view to slot %d\n", slot + 1);
					return;
				}

				// 1~4：切到对应槽位
				if (key >= GLFW_KEY_1 && key <= GLFW_KEY_4)
				{
					const int slot = key - GLFW_KEY_1;
					cameraController.RecallViewSlot(slot);
					std::printf("[Camera] recalled slot %d\n", slot + 1);
					return;
				}

				// 5：回到启动视角
				if (key == GLFW_KEY_5)
				{
					cameraController.ResetToStartupView();
					std::printf("[Camera] restored startup view\n");
				}
			});

		std::printf("[Camera] WASD=up/left/right  Space/Ctrl=forward/back  Shift=fast  wheel=zoom\n");
		std::printf("[Camera] 1-4=recall view  F1-F4=save current view  5=startup view\n");

		// ---- 一次性资源：全部在循环之外 ----
		Shader depthShader("assets/shaders/ShadowDepth.glsl");
		Shader basicShader("assets/shaders/PCSS.glsl");
		basicShader.Bind();
		// 每帧都会变的 uniform（u_ViewProjection / u_Model / ...）放到循环里设，
		// 这里只设全程不变的。u_Texture 是"采样器读哪个纹理单元"，固定为 0
		basicShader.SetFloat4("u_Color", glm::vec4(1.0f));
		basicShader.SetInt("u_Texture", 0);
		// ---- 扫描纹理目录 ----
		TextureSpec colorSpec;
		colorSpec.SRGB      = true;                      // 颜色贴图必须做 sRGB 解码
		colorSpec.MinFilter = TextureFilter::Linear;     // 缩小走 mipmap，避免摩尔纹
		colorSpec.MagFilter = TextureFilter::Nearest;    // 放大保持锐利

		// 只在启动时扫一次：目录 IO + 图片解码 + 显存上传放进每帧循环会直接卡死
		std::vector<TextureEntry> textures =
			LoadTexturesFromDirectory("assets/textures/", colorSpec);

		// 按名字查，不要写 textures[0] —— 目录里加一个文件下标就全乱了
		auto findTexture = [&textures](const std::string& name) -> std::shared_ptr<Texture>
			{
				for (const TextureEntry& entry : textures)
				{
					if (entry.Name == name)
					{
						return entry.Handle;
					}
				}
				return nullptr;
			};

		// ---- 往场景里放东西 ----
		//
		// 共享几何：只上传一份 VAO / VBO / EBO，多个 draw call 复用它。
		//
		// 【配方和几何从同一份 desc 出来】这是刻意的：只要手写一句
		// "Geometry = 某个 mesh"，物体就会保留默认配方（Cube 1.0），
		// 面板于是显示"已同步"却在骗你 —— 不报错，只是不对。
		// 用 SetSharedGeometry 让两份数据一起设置，这类错位就不可能发生
		PrimitiveDesc cubeDesc;                     // 默认就是 Cube / 1.0 / 32 / 16
		PrimitiveDesc sphereDesc;
		sphereDesc.Type = PrimitiveType::Sphere;    // 其余参数保持默认

		auto cubeMesh   = std::make_shared<Mesh>(Mesh::Create(cubeDesc));
		auto sphereMesh = std::make_shared<Mesh>(Mesh::Create(sphereDesc));

		// 【AddObject 是按值传参】cube 会被拷贝进场景，之后这里的 cube 和
		// scene.GetObject(0) 就是两个对象了。所以设置必须在 Add 之前做完；
		// 之后改参数一律走 scene.GetObject(i)，否则改的是副本、画面毫无反应
		{
			RenderObject cube;

			// 材质参数都是【线性空间】的值。别按美术直觉填灰——
			// 那是在 sRGB 空间调出来的，放进线性管线会偏暗
			cube.Name = "Cube-1";
			cube.SetSharedGeometry(cubeMesh, cubeDesc);
			cube.Mat.Ambient   = glm::vec3(0.10f);
			cube.Mat.Diffuse   = glm::vec3(1.00f);
			cube.Mat.Specular  = glm::vec3(0.50f);
			cube.Mat.Shininess = 32.0f;
			cube.Xform.Position = glm::vec3(0.0f, 0.0f, 0.0f);
			cube.Xform.Scale    = glm::vec3(0.5f);
			cube.Mat.AlbedoTexture = findTexture("Checkerboard");

			scene.AddObject(cube);
		}

		//{
		//	RenderObject cube;
		//	cube.Name = "Cube-2";
		//	cube.SetSharedGeometry(cubeMesh, cubeDesc);   // 同一份几何
		//	cube.Mat.Ambient   = glm::vec3(0.10f);
		//	cube.Mat.Diffuse   = glm::vec3(1.00f);
		//	cube.Mat.Specular  = glm::vec3(0.50f);
		//	cube.Mat.Shininess = 32.0f;
		//	cube.Xform.Position = glm::vec3(1.0f, 0.5f, 0.0f);
		//	cube.Xform.Scale    = glm::vec3(0.5f);
		//	cube.Mat.AlbedoTexture = findTexture("Checkerboard");

		//	scene.AddObject(cube);
		//}
		{
			RenderObject platform;
			platform.Name = "Platform-1";
			platform.SetSharedGeometry(cubeMesh, cubeDesc);
			platform.Mat.Ambient = glm::vec3(0.10f);
			platform.Mat.Diffuse = glm::vec3(1.00f);
			platform.Mat.Specular = glm::vec3(0.50f);
			platform.Mat.Shininess = 32.0f;
			// 板厚一半是 0.05，所以顶面在 -0.25 —— 正好和立方体的底面贴合。
			// 用 -0.5 的话顶面在 -0.45，立方体会悬空 0.2；
			// 没有阴影时看不出来，有阴影后会非常刺眼
			platform.Xform.Position = glm::vec3(0.0f, -1.0f, 0.0f);
			platform.Xform.Scale = glm::vec3(100.0f, 0.1f, 100.0f);
			platform.Mat.AlbedoTexture = nullptr;
			scene.AddObject(platform);
		}
		for (uint32_t x = 0; x < 1; x++)
		{
			for (uint32_t y = 0; y <1; y++)
			{
				for (uint32_t z = 0; z < 1; z++)
				{
					RenderObject sphere;
					sphere.Name = "Sphere";
					sphere.SetSharedGeometry(sphereMesh, sphereDesc);

					sphere.Mat.Ambient = glm::vec3(0.10f);
					sphere.Mat.Diffuse = glm::vec3(1.00f);
					sphere.Mat.Specular = glm::vec3(0.50f);
					sphere.Mat.Shininess = 32.0f;
					sphere.Xform.Position = glm::vec3(1.0f+x*0.25f, 0.0f+y*0.25f, 0.0f+z*0.25f);

					// CreateSphere(1.0) 的半径是 1.0（直径 2）。缩到 0.25 之后直径 0.5，
					// 正好和 Cube-1（1.0 边长 × 0.5 缩放）一样大
					sphere.Xform.Scale = glm::vec3(0.1f);
					sphere.Mat.AlbedoTexture = nullptr;

					scene.AddObject(sphere);

				}

			}
		}
		//{
		//	RenderObject sphere;
		//	sphere.Name = "Sphere-1";
		//	sphere.SetSharedGeometry(sphereMesh, sphereDesc);

		//	sphere.Mat.Ambient   = glm::vec3(0.10f);
		//	sphere.Mat.Diffuse   = glm::vec3(1.00f);
		//	sphere.Mat.Specular  = glm::vec3(0.50f);
		//	sphere.Mat.Shininess = 32.0f;

		//	// 【位置必须在视锥内】原来写的 (5, 5, 0) 从相机 (0,0,3) 望过去
		//	// 偏轴约 67 度，而 45 度垂直视场只有 22.5 度半角 ——
		//	// 即使 mesh 改对了也看不见它。
		//	// 摆在 Cube-1 左边，两个形状并排好对比
		//	sphere.Xform.Position = glm::vec3(-1.3f, 0.0f, 0.0f);

		//	// CreateSphere(1.0) 的半径是 1.0（直径 2）。缩到 0.25 之后直径 0.5，
		//	// 正好和 Cube-1（1.0 边长 × 0.5 缩放）一样大
		//	sphere.Xform.Scale = glm::vec3(0.25f);
		//	sphere.Mat.AlbedoTexture = nullptr;

		//	scene.AddObject(sphere);
		//}
		// ---- 光源 ----
		{
			Light light;

			// 【必须用平行光】SceneRenderer::ComputeLightSpaceMatrix 用的是
			// glm::ortho（正交投影）—— 那是平行光的做法：它没有位置、只有方向，
			// 打在场景上的"光柱截面"大小恒定。
			// 点光要透视投影 + 立方体贴图（6 个面），是另一个量级的复杂度
			light.type = LightType::Directional;

			// direction 存的是"光【射向】哪边"。斜着射下来影子才有立体感；
			// 正上方直射会让光照相机的 up 向量退化，也看不出光照方向
			light.direction = glm::normalize(glm::vec3(-1.0f, -1.5f, -1.0f));

			// 【关键】FindShadowLight 只挑 enabled && castShadow 的灯。
			// 忘了这一句，shadow pass 会在第一行就 return ——
			// 深度图是未定义内容、整个阴影分支也不执行，而且【什么都不报】
			light.castShadow = true;

			light.color = glm::vec3(1.0f, 0.97f, 0.9f);
			// 平行光不乘衰减（这是对的：无穷远光源没有距离概念），
			// 所以亮度就是 intensity 本身，和之前的点光不可比
			light.intensity = 1.5f;

			scene.AddLight(std::move(light));
		}

		// ===================================================================
		//  主循环
		// ===================================================================
		double lastTime = Window::GetTime();

		while (!window.ShouldClose())
		{
			const double now = Window::GetTime();
			const float  ts  = static_cast<float>(now - lastTime);
			lastTime = now;

			window.PollEvents();

			// 必须在任何 ImGui:: 调用之前完成，顺序错了会直接断言失败
			imGuiLayer.BeginFrame();

			// ---- 1. 输入 ----
			// 鼠标在哪个窗口上，用的是【上一帧】UI 构建的结果（见 EditorUI::WasViewportHovered）
			const InputState input = PollInput(window, io, editor.WasViewportHovered());
			cameraController.OnUpdate(input, ts);

			// ---- 2. 更新世界 ----
			// 必须排在渲染之前：渲染阶段只读，它决定"这一帧世界是什么样"
			UpdateSceneAnimation(scene, now);

			// ---- 3. 渲染场景到 FBO ----
			sceneRenderer.RenderShadowPass(scene, shadowMap, depthShader, renderSettings);
			sceneRenderer.RenderSceneByPCSS(scene, sceneBuffer, shadowMap, basicShader, renderSettings);

			// ---- 4. 后处理 ----
			//
			// 【这里没有清屏】全屏三角形覆盖每一个像素，再清一次是纯多出来的
			// 一次全屏写
			//
			// 【关掉后处理时显示 sceneBuffer】这是这一步唯一的验收手段：
			//   关 -> 看到的是线性值直接当 sRGB 显示，会【明显发暗、发灰】
			//   开 -> 和改动前完全一样
			const FrameBuffer* display = &sceneBuffer;
			if (renderSettings.PostProcessEnabled)
			{
				postProcess.Render(sceneBuffer, postProcessBuffer, renderSettings);
				display = &postProcessBuffer;
			}

			// ---- 5. 构建 UI ----
			// 顺序很关键：必须在渲染【之后】。反过来的话 Scene 面板里拿到的
			// 是上一帧的图像，拖相机会有拖影感
			//
			// 【必须传 *display，不能传 sceneBuffer】
			// 传 sceneBuffer 的话，后处理的结果虽然算出来了、也写进了
			// postProcessBuffer，却【没有任何人看它】—— 表现就是
			// "后处理开关拖来拖去画面完全不动"，而且不报错、不警告。
			// 算好了指针却没拿它去用，是这类"管线接对了、最后一步接错"的典型
			editor.Draw(scene, textures, *display, shadowMap, postProcess.GetBloomBuffer(), renderSettings);

			// ---- 6. 提交 ----
			// EndFrame 内部依次做：Render() -> 恢复视口为窗口尺寸 -> 清屏 -> RenderDrawData
			imGuiLayer.EndFrame();

			window.SwapBuffers();
		}
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "[fatal] %s\n", e.what());
		return 1;
	}

	return 0;
}
