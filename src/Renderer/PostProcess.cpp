#include "Renderer/PostProcess.h"
#include "Renderer/RenderCommand.h"
#include <algorithm>


namespace
{

	struct LevelSize 
	{
		uint32_t Width;
		uint32_t Height;
	};
	LevelSize ComputeLevelSize(uint32_t width, uint32_t height, int level)
	{
		const uint32_t divisor = 1u << (level + 1);   // level 0 -> /2，level 1 -> /4 ...
		return LevelSize{
			std::max(width / divisor, 1u),
			std::max(height / divisor, 1u)
		};
	}
	FrameBufferSpec MakeLevelSpec(uint32_t width, uint32_t height, int level)
	{
		const LevelSize size = ComputeLevelSize(width, height, level);

		FrameBufferSpec spec;
		spec.Width = size.Width;
		spec.Height = size.Height;
		spec.DepthOnly = false;

		// 【必须 16F】里面装的是线性光，超过 1 的部分正是"发光"本身 ——
		// 用 RGBA8 会把要提取的东西当场截掉
		spec.ColorFormat = TextureFormat::RGBA16F;

		return spec;
	}
	FrameBufferSpec MakeOutputSpec(uint32_t width, uint32_t height)
	{
		FrameBufferSpec spec;
		spec.Width = width;
		spec.Height = height;
		spec.DepthOnly = false;
		spec.ColorFormat = TextureFormat::RGBA8;
		return spec;

	}

}



PostProcess::PostProcess(uint32_t width, uint32_t height)
	: m_DownsampleShader("assets/shaders/BloomDownsample.glsl")
	, m_UpsampleShader("assets/shaders/BloomUpsample.glsl")
	, m_CompositeShader("assets/shaders/PostProcess.glsl")
	,m_FxaaShader("assets/shaders/FXAA.glsl")
	, m_Output(MakeOutputSpec(width,height))
{
	// 【文件名大小写必须和磁盘上完全一致】Windows 不区分大小写，
	// 写错也能加载成功 —— 于是这个错在 Windows 上永远不会暴露

	// sampler uniform 不随帧变化，构造函数里设一次就够。
	// 合成那一遍要读两张：场景在 0 号单元、bloom 在 1 号单元
	//
	// 【名字必须和 shader 里声明的一字不差】写错了不报错：
	// glGetUniformLocation 返回 -1，glUniform 在 -1 上是静默空操作
	m_DownsampleShader.SetInt("u_Source", 0);
	m_UpsampleShader.SetInt("u_Source", 0);
	m_CompositeShader.SetInt("u_SceneTexture", 0);
	m_CompositeShader.SetInt("u_BloomTexture", 1);
	m_FxaaShader.SetInt("u_SceneTexture", 0);

	// 一次把上限级数全建出来。
	// 面板上的"级数"只控制用几级，不重新分配
	m_Levels.reserve(k_MaxLevel);
	for (int level = 0; level < k_MaxLevel; ++level)
	{
		m_Levels.push_back(std::make_unique<FrameBuffer>(
			MakeLevelSpec(width, height, level)));
	}
	m_Output.Resize(width, height);
}

void PostProcess::Resize(uint32_t width, uint32_t height)
{
	// 【必须自己挡一次 0】0 是"窗口被最小化"，不是真想渲染到 0 像素。
	// ComputeLevelSize 里的 std::max(.., 1u) 是为了"级数很深时别除出 0"，
	// 不是为最小化准备的 —— 不挡的话窗口一最小化，金字塔会被缩成 1x1，
	// 而 sceneBuffer 保持原尺寸，两者的比例关系当场错位
	if (width == 0 || height == 0)
	{
		return;
	}

	for (int level = 0; level < k_MaxLevel; ++level)
	{
		const LevelSize size = ComputeLevelSize(width, height, level);
		// FrameBuffer::Resize 内部已经做了"尺寸没变就跳过"
		m_Levels[level]->Resize(size.Width, size.Height);
	}
}

void PostProcess::Blit(Shader& shader, const FrameBuffer& target,
	uint32_t source0, uint32_t source1)
{
	target.Bind();

	// 视口必须自己设。【金字塔里尤其关键】——
	// 每一级的尺寸都不一样，忘了设就是"用上一级的尺寸渲染本级的图"
	RenderCommand::SetViewport(0, 0,
		static_cast<int32_t>(target.GetWidth()),
		static_cast<int32_t>(target.GetHeight()));

	// pass hygiene：全屏 pass 和深度无关。
	// 【这里不逐个恢复】一个 Render 里有 10 个 pass，
	// 统一在 Render() 末尾恢复一次就够
	RenderCommand::SetDepthTest(false);
	RenderCommand::SetDepthWrite(false);

	RenderCommand::BindTexture(source0, 0);
	if (source1 != 0)
	{
		RenderCommand::BindTexture(source1, 1);
	}

	shader.Bind();
	RenderCommand::DrawArrays(m_FullScreenVAO, 3);
}

void PostProcess::Render(const FrameBuffer& scene, const FrameBuffer& target, const RenderSettings& settings)
{
	const int levels = std::clamp(settings.BloomLevels, 1, k_MaxLevel);
	const bool bloomOn = settings.BloomEnabled;
	if (bloomOn)
	{
		// 1. Downsample
		for (int level = 0; level < levels; ++level)
		{
			const uint32_t source = (level == 0) ? scene.GetColorAttachmentID() : m_Levels[level - 1]->GetColorAttachmentID();
			const bool isBrightPass = (level == 0);
			m_DownsampleShader.SetInt("u_ApplyThreshold", isBrightPass ? 1 : 0);
			m_DownsampleShader.SetFloat("u_Exposure", settings.Exposure);
			m_DownsampleShader.SetFloat("u_Threshold", settings.BloomThreshold);
			m_DownsampleShader.SetFloat("u_Knee", settings.BloomKnee);
			Blit(m_DownsampleShader, *m_Levels[level], source);
		}
		//2. Upsample
		RenderCommand::SetAdditiveBlend(true);
		for(int level = levels-1; level > 0; --level)
		{
			m_UpsampleShader.SetFloat("u_Radius", settings.BloomRadius);
			m_UpsampleShader.SetFloat("u_Scatter", settings.BloomScatter);
			Blit(m_UpsampleShader, *m_Levels[level - 1], m_Levels[level]->GetColorAttachmentID());
		}
		RenderCommand::SetAdditiveBlend(false);

	}
	
	m_CompositeShader.SetFloat("u_Exposure", settings.Exposure);
	m_CompositeShader.SetFloat("u_BloomStrength", bloomOn? settings.BloomStrength:0.0f);
	m_CompositeShader.SetInt("u_TonemapMode", settings.TonemapMode);
	m_CompositeShader.SetInt("u_DebugHDRView", settings.DebugHDRView);

	// 这一遍读两张：场景（单元 0）+ bloom L0（单元 1）
	Blit(m_CompositeShader, target,
		scene.GetColorAttachmentID(),
		m_Levels[0]->GetColorAttachmentID());
	m_FxaaShader.SetFloat2("u_TexelSize", glm::vec2(1.0f / static_cast<float>(m_Output.GetWidth()),
		1.0f / static_cast<float>(m_Output.GetHeight())));
	m_FxaaShader.SetFloat("u_EdgeThreshold", settings.FxaaEdgeThreshold);
	m_FxaaShader.SetFloat("u_EdgeThresholdMin", settings.FxaaEdgeThresholdMin);
	m_FxaaShader.SetInt("u_FxaaEnabled", settings.FxaaEnabled ? 1 : 0);
	m_FxaaShader.SetInt("u_DebugEdges", settings.DebugEdges);
	Blit(m_FxaaShader, m_Output, target.GetColorAttachmentID());
	// ---- 统一恢复 ----
	// 这些开关都是【全局】的。不恢复的话症状离原因很远
	RenderCommand::SetDepthTest(true);
	RenderCommand::SetDepthWrite(true);
	target.Unbind();
}