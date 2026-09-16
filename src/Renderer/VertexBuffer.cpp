#include "VertexBuffer.h"
#include <glad/glad.h>

VertexBuffer::VertexBuffer()
{
	glGenBuffers(1, &m_RendererID);
}


VertexBuffer::~VertexBuffer()
{
	if (m_RendererID != 0)
	{
		glDeleteBuffers(1, &m_RendererID);
		m_RendererID = 0;
	}
}


void VertexBuffer::Bind() const
{
	glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
}

void VertexBuffer::Unbind() const
{
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::SetData(const void* data, uint32_t size)
{
	if (data == nullptr || size == 0)
	{
		return;
	}

	// glGenBuffers 只是创建缓冲区对象，数据存储大小是 0，必须先分配存储才能往里写，
	// 否则 glBufferSubData 越界会报 GL_INVALID_VALUE，数据被静默丢弃。
	Bind();

	if (size > m_Capacity)
	{
		// 多留 50% 余量，避免顶点数小幅波动时反复重新分配显存
		m_Capacity = size + size / 2;
		glBufferData(GL_ARRAY_BUFFER, m_Capacity, nullptr, GL_DYNAMIC_DRAW);
	}

	// 容量够时原地更新，不再重新分配显存
	glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
}
