#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include "Core/Vertex.h"
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
	static Mesh CreateCube(float size = 1.0f);
	std::vector<Vertex>& Vertices() { return m_Vertices; }
	const std::vector<Vertex>& GetVertices() { return m_Vertices; }
	const std::vector<uint32_t>& GetIndices() { return m_Indices; }
	uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_Vertices.size()); }
	uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }
	void UploadVertexData();
	void Draw() const;
private:
	void SetupBuffers();
private:
	std::vector<Vertex> m_Vertices;
	std::vector<uint32_t> m_Indices;
	std::shared_ptr<VertexBuffer> m_VertexBuffer;
	std::shared_ptr<IndexBuffer> m_IndexBuffer;
	std::shared_ptr<VertexArray> m_VertexArray;
};

