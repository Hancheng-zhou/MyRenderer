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
			if (window.IsKeyPressed(GLFW_KEY_W))            { input.MoveAxis.y += 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_S))            { input.MoveAxis.y -= 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_D))            { input.MoveAxis.z += 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_A))            { input.MoveAxis.z -= 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_SPACE))        { input.MoveAxis.x += 1.0f; }
			if (window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) { input.MoveAxis.x -= 1.0f; }
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
		RenderCommand::SetClearColor(glm::vec4(0.09f, 0.09f, 0.11f, 1.0f));

		// 规则 4：ImGuiLayer 必须晚于 Window ——
		// 它的平台后端会接管 Window 已经装好的回调，顺序反了 ImGui 就收不到输入
		ImGuiLayer imGuiLayer(window);
		ImGuiIO& io = ImGui::GetIO();

		// ---- 离屏渲染目标 ----
		// 场景先渲染到 FBO，再由 ImGui 当成纹理显示出来。
		// 这一步不只是"换个地方画"：一旦画面经过 FBO，就能在它与屏幕之间
		// 插入后处理（bloom / 色调映射 / SSAO ...）
		FrameBuffer frameBuffer(window.GetWidth(), window.GetHeight());

		// ---- 场景：相机、物体、光源都收在这里 ----
		// 宽高比取自 FBO 而不是窗口：真正决定画面比例的是渲染目标的尺寸
		const float initialAspect = frameBuffer.GetHeight() > 0
			? static_cast<float>(frameBuffer.GetWidth()) / static_cast<float>(frameBuffer.GetHeight())
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
		window.SetResizeCallback([&scene, &frameBuffer](uint32_t width, uint32_t height)
			{
				// 回调参数已经是 framebuffer 尺寸（像素）。
				// FBO 尺寸和相机宽高比必须一起更新，少改一个画面就会拉伸
				frameBuffer.Resize(width, height);

				// 这里读 FBO 的尺寸而不是回调参数：窗口最小化时 FBO 会拒绝变成 0，
				// 相机的宽高比也该跟着保持原样
				scene.GetCamera().SetViewportSize(frameBuffer.GetWidth(), frameBuffer.GetHeight());
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
		Shader shader("assets/shaders/Basic.glsl");
		shader.Bind();
		// 每帧都会变的 uniform（u_ViewProjection / u_Model / ...）放到循环里设，
		// 这里只设全程不变的。u_Texture 是"采样器读哪个纹理单元"，固定为 0
		shader.SetFloat4("u_Color", glm::vec4(1.0f));
		shader.SetInt("u_Texture", 0);

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
		// 两个物体共享同一份几何：只上传一份 VBO/VAO，两个 draw call 复用它
		auto cubeMesh = std::make_shared<Mesh>(Mesh::CreateCube(1.0f));

		// 【AddObject 是按值传参】cube 会被拷贝进场景，之后这里的 cube 和
		// scene.GetObject(0) 就是两个对象了。所以设置必须在 Add 之前做完；
		// 之后改参数一律走 scene.GetObject(i)，否则改的是副本、画面毫无反应
		{
			RenderObject cube;

			// 材质参数都是【线性空间】的值。别按美术直觉填灰——
			// 那是在 sRGB 空间调出来的，放进线性管线会偏暗
			cube.Geometry      = cubeMesh;
			cube.Mat.Ambient   = glm::vec3(0.10f);
			cube.Mat.Diffuse   = glm::vec3(1.00f);
			cube.Mat.Specular  = glm::vec3(0.50f);
			cube.Mat.Shininess = 32.0f;
			cube.Xform.Position = glm::vec3(0.0f, 0.0f, 0.0f);
			cube.Xform.Scale    = glm::vec3(0.5f);
			cube.Mat.AlbedoTexture = findTexture("photo");

			scene.AddObject(cube);
		}

		{
			RenderObject cube;

			cube.Geometry      = cubeMesh;               // 同一份几何
			cube.Mat.Ambient   = glm::vec3(0.10f);
			cube.Mat.Diffuse   = glm::vec3(1.00f);
			cube.Mat.Specular  = glm::vec3(0.50f);
			cube.Mat.Shininess = 32.0f;
			cube.Xform.Position = glm::vec3(1.0f, 0.5f, 0.0f);
			cube.Xform.Scale    = glm::vec3(0.5f);
			cube.Mat.AlbedoTexture = findTexture("Checkerboard");

			scene.AddObject(cube);
		}
		{
			RenderObject platform;
			platform.Geometry = cubeMesh;
			platform.Mat.Ambient = glm::vec3(0.10f);
			platform.Mat.Diffuse = glm::vec3(1.00f);
			platform.Mat.Specular = glm::vec3(0.50f);
			platform.Mat.Shininess = 32.0f;
			platform.Xform.Position = glm::vec3(0.0f, -0.5f, 0.0f);
			platform.Xform.Scale = glm::vec3(10.0f, 0.1f, 10.0f);
			platform.Mat.AlbedoTexture = nullptr;
			scene.AddObject(platform);
		}

		// ---- 光源 ----
		{
			Light light;
			light.type = LightType::Point;
			// 位置【不能放在原点】：那里正是立方体中心，
			// L = lightPos - worldPos 会退化成零向量，normalize 之后全是 NaN
			light.position  = glm::vec3(2.0f, 3.0f, 2.0f);
			light.color     = glm::vec3(0.9f, 0.0f, 0.0f);
			light.intensity = 1.0f;

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
			sceneRenderer.RenderScene(scene, frameBuffer, shader, renderSettings);

			// ---- 4. 构建 UI ----
			// 顺序很关键：必须在渲染【之后】。反过来的话 Scene 面板里拿到的
			// 是上一帧的图像，拖相机会有拖影感
			editor.Draw(scene, textures, frameBuffer, renderSettings);

			// ---- 5. 提交 ----
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
