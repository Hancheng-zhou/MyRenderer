#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include "Core/Vertex.h"
#include "Core/PrimitiveDesc.h"
class VertexArray;
class VertexBuffer;
class IndexBuffer;
class Mesh
{
public:
	Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices);
	~Mesh() = default;
	Mesh(const Mesh&) = delete;
	Mesh& operator=(const Mesh&) = delete;
	Mesh(Mesh&&) noexcept = default;
	Mesh& operator=(Mesh&&) noexcept = default;
	// 唯一的"配方 → 几何"入口。调用方（面板、场景初始化）只需要传一份
	// PrimitiveDesc，不需要知道有几种几何；加形状只改这个 switch 和
	// PrimitiveDesc.h，调用侧一行都不用动
	static Mesh Create(const PrimitiveDesc& desc);

	// 具体的生成函数。一般用上面的 Create 就够了，直接调它们的情况是
	// "我明确知道要什么形状"，比如场景初始化里那几份共享几何
	static Mesh CreateCube(float size = 1.0f);
	static Mesh CreateSphere(float radius = 1.0f, uint32_t segments = 32, uint32_t rings = 16);
	std::vector<Vertex>& Vertices() { return m_Vertices; }
	const std::vector<Vertex>& GetVertices() { return m_Vertices; }
	const std::vector<uint32_t>& GetIndices() { return m_Indices; }
	uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_Vertices.size()); }
	uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }
	void UploadVertexData();
	void Draw() const;
private:
	void SetupBuffers();
	void ComputeTangents();
private:
	std::vector<Vertex> m_Vertices;
	std::vector<uint32_t> m_Indices;
	std::shared_ptr<VertexBuffer> m_VertexBuffer;
	std::shared_ptr<IndexBuffer> m_IndexBuffer;
	std::shared_ptr<VertexArray> m_VertexArray;
};

