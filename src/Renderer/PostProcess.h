#pragma once
#include "Renderer/VertexArray.h"
#include "Renderer/Shader.h"
#include "Renderer/RenderSettings.h"
class FrameBuffer;
class PostProcess
{
public:
	PostProcess();
	void Render(const FrameBuffer& source, const FrameBuffer& target,const RenderSettings& settings);

private:
	Shader m_PostProcessShader;
	VertexArray m_FullScreenVAO;
};

