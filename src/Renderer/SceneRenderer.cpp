#include "SceneRenderer.h"

#include "Renderer/FrameBuffer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"   // Material.h 只前向声明了 Texture，用 ->Bind() 要完整类型
#include "Scene/Scene.h"

void SceneRenderer::PackLights(const Scene& scene)
{
	m_LightTypes.clear();       m_LightPositions.clear();
	m_LightDirections.clear();  m_LightColors.clear();
	m_LightIntensities.clear(); m_LightAttenuations.clear();
	m_LightCutoffs.clear();

	for (const Light& light : scene.GetLights())
	{
		// enabled 在 CPU 侧过滤，不进数组 ——
		// 传进去再在着色器里 if 跳过，等于白占一个数组槽位
		if (!light.enabled)
		{
			continue;
		}

		// 超上限就截断，别让 uniform 数组越界。
		// 面板那边已经把 Add Light 按钮禁掉了，这里是第二道防线
		if (m_LightTypes.size() >= Scene::k_MaxLightsCount)
		{
			break;
		}

		m_LightTypes.push_back(static_cast<int>(light.type));
		m_LightPositions.push_back(light.position);
		m_LightDirections.push_back(light.direction);
		m_LightColors.push_back(light.color);
		m_LightIntensities.push_back(light.intensity);
		m_LightAttenuations.push_back(glm::vec3(light.constant, light.linear, light.quadratic));
		m_LightCutoffs.push_back(glm::vec2(light.innerCutoff, light.outerCutoff));
	}
}

void SceneRenderer::UploadLights(Shader& shader)
{
	const auto lightCount = static_cast<uint32_t>(m_LightTypes.size());

	shader.SetInt("u_LightCount", static_cast<int>(lightCount));

	if (lightCount == 0)
	{
		// 空数组时不能取 data()[0]，Array 接口也就都不用调了。
		// 着色器那边的循环上界是 u_LightCount，所以不设也不会有残留问题
		return;
	}

	// 传的是同一个 lightCount：这样"7 个数组数量必须一致"变成
	// 结构上不可能出错，而不是靠人抄对 7 次
	shader.SetIntArray   ("u_LightType",        m_LightTypes.data(),        lightCount);
	shader.SetFloat3Array("u_LightPosition",    m_LightPositions.data(),    lightCount);
	shader.SetFloat3Array("u_LightDirection",   m_LightDirections.data(),   lightCount);
	shader.SetFloat3Array("u_LightColor",       m_LightColors.data(),       lightCount);
	shader.SetFloatArray ("u_LightIntensity",   m_LightIntensities.data(),  lightCount);
	shader.SetFloat3Array("u_LightAttenuation", m_LightAttenuations.data(), lightCount);
	shader.SetFloat2Array("u_LightCutoff",      m_LightCutoffs.data(),      lightCount);
}

void SceneRenderer::RenderScene(const Scene& scene,
                                const FrameBuffer& target,
                                Shader& shader,
                                const RenderSettings& settings)
{
	// ---- 绑定渲染目标 ----
	target.Bind();

	// 视口要跟着渲染目标走：绑了 FBO 却还用窗口尺寸设视口，
	// 画面只会占 FBO 的一角，或者被裁掉一部分
	RenderCommand::SetViewport(0, 0,
	                           static_cast<int32_t>(target.GetWidth()),
	                           static_cast<int32_t>(target.GetHeight()));
	RenderCommand::Clear();

	// ---- 绑定着色器 ----
	// 【顺序约束】这一句必须在所有 glUniform* 之前。
	// glUniform* 作用于"当前绑定的 program"，放到 Bind 之前会设到别的 program 上 ——
	// 而且不报错，只是静默失效（或污染别人的 uniform）
	shader.Bind();

	const Camera& camera = scene.GetCamera();

	// 投影 × 视图：相机每帧可能都在动，必须每帧重新上传
	shader.SetMat4("u_ViewProjection", camera.GetViewProjection());
	shader.SetInt ("u_GammaCorrect", settings.GammaCorrect ? 1 : 0);

	// 相机位置：算高光需要视线方向 V = normalize(cameraPos - worldPos)
	shader.SetFloat3("u_CameraPos", camera.GetPosition());

	// ---- 光源：整场共享，与具体物体无关，所以放在遍历物体之前 ----
	PackLights(scene);
	UploadLights(shader);

	// ---- 逐个物体提交绘制 ----
	// 从"画一个立方体"到"画一个场景"的分水岭就是这个循环
	for (const RenderObject& object : scene.GetObjects())
	{
		if (!object.Geometry||!object.IsVisible)
		{
			continue;   // 只占位、还没挂几何的对象，跳过
		}

		// 贴图是【每个材质】的资源，每个物体各自绑。
		//
		// 槽位固定用 0：一次 draw call 只有一个采样器在采样，
		// 没理由让槽位号跟着物体编号走 —— 那会白白消耗纹理单元
		//（GL 只保证 fragment 阶段至少 16 个，超了就静默采样错）
		if (object.Mat.AlbedoTexture)
		{
			object.Mat.AlbedoTexture->Bind(0);
			shader.SetInt("u_UseTex", 1);
		}
		else
		{
			// 必须显式关掉：uniform 一旦设进 program 就会一直保留，
			// 不重设就会残留上一个物体的值
			shader.SetInt("u_UseTex", 0);
		}

		// 模型矩阵来自物体自己的 Transform：位置 / 旋转 / 缩放都在里面
		shader.SetMat4("u_Model", object.Xform.GetModelMatrix());

		// 法线矩阵别偷懒写成 mat3(model)：那只在"旋转 + 均匀缩放"时成立。
		// 一旦出现非均匀缩放，法线会被拉歪，光照方向整个错掉。
		// GetNormalMatrix() 内部算的就是 transpose(inverse(mat3(model)))
		shader.SetMat3("u_NormalMatrix", object.Xform.GetNormalMatrix());

		// 材质是【每个物体】自己的属性，必须紧挨着它的 draw call 设置
		shader.SetFloat3("u_Ambient",   object.Mat.Ambient);
		shader.SetFloat3("u_Diffuse",   object.Mat.Diffuse);
		shader.SetFloat3("u_Specular",  object.Mat.Specular);
		shader.SetFloat ("u_Shininess", object.Mat.Shininess);

		object.Geometry->Draw();
	}

	target.Unbind();
}
