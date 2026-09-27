#pragma once
#include <memory>
#include <vector>
#include "VertexBuffer.h"
#include "IndexBuffer.h"
class VertexArray
{
public:
	VertexArray();
	~VertexArray();
	VertexArray(const VertexArray&) = delete;
	VertexArray& operator=(const VertexArray&) = delete;
	void Bind();
	void Unbind();
	void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer);
	void AddIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer);
	const std::vector<std::shared_ptr<VertexBuffer>>& GetVertexBuffers() const { return m_VertexBuffers; }
	const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const { return m_IndexBuffer; }
	uint32_t GetRendererID() const { return m_RendererID; }
private:
	uint32_t m_RendererID = 0;
	uint32_t m_VertexBufferIndex = 0;
	std::vector<std::shared_ptr<VertexBuffer>> m_VertexBuffers;
	std::shared_ptr<IndexBuffer> m_IndexBuffer;
};

