#pragma once
#include  "Core/BufferCommon.h"
class VertexBuffer
{
public:
	VertexBuffer();
	~VertexBuffer();

	// 持有 GL 对象的所有权，禁止拷贝：拷贝会让两个对象持同一个缓冲区 ID，析构时被释放两次
	VertexBuffer(const VertexBuffer&) = delete;
	VertexBuffer& operator=(const VertexBuffer&) = delete;

	void Bind() const;
	void Unbind() const;
	const BufferLayout& GetBufferLayout() const{ return m_BufferLayout; }
	void SetLayout(const BufferLayout& layout) { m_BufferLayout = layout; }
	// 上传顶点数据：容量够就原地更新，不够才重新分配显存
	void SetData(const void* data, uint32_t size);
	const uint32_t GetRendererID() const { return m_RendererID;}
	const uint32_t GetCapacity() const { return m_Capacity; }
private:
	BufferLayout m_BufferLayout;
	uint32_t m_RendererID = 0;
	uint32_t m_Capacity = 0;   // 已分配的字节数（不是顶点个数）
};




