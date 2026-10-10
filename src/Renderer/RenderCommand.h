#pragma once
#include <glm/glm.hpp>
#include <cstdint>
class VertexArray;
class RenderCommand
{
public:
	static void Init();
	static void SetViewport(int32_t x, int32_t y, int32_t width, int32_t height);
	static void SetDepthTest(bool enable);
	static void SetDepthWrite(bool enable);
	static void SetClearColor(const glm::vec4& color);
	static void Clear();
	static void ClearDepth();
	static void SetCullMode(bool cullFront);
	static void BindTexture(uint32_t rendererID, uint32_t slot);
	static void DrawArrays(const VertexArray& vertexArray, uint32_t vertexCount);
	static void DrawIndexed(const VertexArray& vertexArray, uint32_t indexCount);
};

