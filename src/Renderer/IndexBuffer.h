#pragma once
#include <stdint.h>

class IndexBuffer
{
public:
	IndexBuffer(uint32_t* indices, uint32_t count);
	~IndexBuffer();
	IndexBuffer(const IndexBuffer&) = delete;
	IndexBuffer& operator=(const IndexBuffer&) = delete;
	void Bind();
	uint32_t GetRendererID() const { return m_RendererID; }
	uint32_t GetCount() const { return m_Count; }
private:
	uint32_t m_RendererID = 0;
    uint32_t m_Count = 0;
};

