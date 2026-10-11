#pragma once
#include "Renderer/VertexArray.h"
#include "Renderer/Shader.h"
#include "Renderer/RenderSettings.h"
#include "Renderer/FrameBuffer.h"
class FrameBuffer;
class PostProcess
{
public:
	PostProcess(uint32_t width, uint32_t height);
	void Resize(uint32_t width, uint32_t height);
	void Render(const FrameBuffer& scene, const FrameBuffer& target,const RenderSettings& settings);
	const FrameBuffer& GetBloomBuffer() const { return *m_Levels[0]; }
	const FrameBuffer& GetOutput() const { return m_Output; }
	static constexpr int k_MaxLevel = 6;
private:
	void Blit(Shader& shader,
		const FrameBuffer& target,
		uint32_t source0,
		uint32_t source1 =0
		);
private:
	Shader m_DownsampleShader;
	Shader m_UpsampleShader;
	Shader m_CompositeShader;
	Shader m_FxaaShader;
	FrameBuffer m_Output;
	VertexArray m_FullScreenVAO;
	std::vector<std::unique_ptr<FrameBuffer>> m_Levels;
};

