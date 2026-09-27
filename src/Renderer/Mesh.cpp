#include "Mesh.h"
#include <utility>
#include "IndexBuffer.h"
#include "VertexBuffer.h"
#include "VertexArray.h"
#include "RenderCommand.h"
Mesh::Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices)
	:m_Vertices(std::move(vertices)),m_Indices(std::move(indices))
{
	SetupBuffers();
}

Mesh Mesh::CreateCube(float size)
{
	// 立方体以【原点为中心】，size 是边长。
	// 居中很关键：绕任意轴旋转、以及围绕它做缩放时，原点在中心才不会"甩出去"
	const float h = size * 0.5f;

	// 24 个顶点（不是 8 个）：每个面必须有自己独立的 4 个顶点，
	// 因为法线和 UV 是"按面"的。共享 8 个角点的话，一个角点只能有
	// 一个法线，三个相邻面就会互相污染，光照会出现明显的错误渐变
	//
	// 每个面内部按【左下 -> 右下 -> 右上 -> 左上】排列。
	// 这个顺序从面的【外部】看是逆时针的，配合 glFrontFace(GL_CCW)
	// 与背面剔除，才能保证正面朝外。
	// UV 也就顺势是 (0,0) (1,0) (1,1) (0,1)，六个面共用同一套排列
	std::vector<Vertex> vertices =
	{
		// +Z 面（前），法线朝 +Z
		{ { -h, -h,  h }, {  0,  0,  1 }, { 0, 0 } },
		{ {  h, -h,  h }, {  0,  0,  1 }, { 1, 0 } },
		{ {  h,  h,  h }, {  0,  0,  1 }, { 1, 1 } },
		{ { -h,  h,  h }, {  0,  0,  1 }, { 0, 1 } },

		// -Z 面（后），法线朝 -Z。
		// 注意起点换成了 +X 那一侧：从背后看过去，+X 才是左手边，
		// 这样排列才能从外部看仍然保持逆时针
		{ {  h, -h, -h }, {  0,  0, -1 }, { 0, 0 } },
		{ { -h, -h, -h }, {  0,  0, -1 }, { 1, 0 } },
		{ { -h,  h, -h }, {  0,  0, -1 }, { 1, 1 } },
		{ {  h,  h, -h }, {  0,  0, -1 }, { 0, 1 } },

		// +X 面（右）
		{ {  h, -h,  h }, {  1,  0,  0 }, { 0, 0 } },
		{ {  h, -h, -h }, {  1,  0,  0 }, { 1, 0 } },
		{ {  h,  h, -h }, {  1,  0,  0 }, { 1, 1 } },
		{ {  h,  h,  h }, {  1,  0,  0 }, { 0, 1 } },

		// -X 面（左）
		{ { -h, -h, -h }, { -1,  0,  0 }, { 0, 0 } },
		{ { -h, -h,  h }, { -1,  0,  0 }, { 1, 0 } },
		{ { -h,  h,  h }, { -1,  0,  0 }, { 1, 1 } },
		{ { -h,  h, -h }, { -1,  0,  0 }, { 0, 1 } },

		// +Y 面（上）
		{ { -h,  h,  h }, {  0,  1,  0 }, { 0, 0 } },
		{ {  h,  h,  h }, {  0,  1,  0 }, { 1, 0 } },
		{ {  h,  h, -h }, {  0,  1,  0 }, { 1, 1 } },
		{ { -h,  h, -h }, {  0,  1,  0 }, { 0, 1 } },

		// -Y 面（下）
		{ { -h, -h, -h }, {  0, -1,  0 }, { 0, 0 } },
		{ {  h, -h, -h }, {  0, -1,  0 }, { 1, 0 } },
		{ {  h, -h,  h }, {  0, -1,  0 }, { 1, 1 } },
		{ { -h, -h,  h }, {  0, -1,  0 }, { 0, 1 } },
	};

	// 每个面拆成两个三角形：0-1-2 与 0-2-3。
	//
	// 千万别写成 0-1-2 / 1-2-3：那样第二个三角形相对第一个是翻转的，
	// 绕序不一致，结果就是一个三角形正面朝外、另一个朝里，
	// 后者会被背面剔除吃掉 —— 表现是"每个面都缺一半"。
	// 用 0-2-3 的共享边方式，两个三角形绕序才一致
	std::vector<uint32_t> indices;
	indices.reserve(36);
	for (uint32_t face = 0; face < 6; ++face)
	{
		const uint32_t base = face * 4;

		indices.push_back(base + 0);
		indices.push_back(base + 1);
		indices.push_back(base + 2);

		indices.push_back(base + 0);
		indices.push_back(base + 2);
		indices.push_back(base + 3);
	}

	// C++17 保证这里不会发生拷贝也不会发生移动，直接在调用处构造
	return Mesh(std::move(vertices), std::move(indices));
}

void Mesh::SetupBuffers()
{
	m_VertexArray = std::make_shared<VertexArray>();
	m_VertexBuffer = std::make_shared<VertexBuffer>();
	m_IndexBuffer = std::make_shared<IndexBuffer>(m_Indices.data(),GetIndexCount());
	m_VertexBuffer->SetLayout
	(
		{
			{ShaderDataType::Float3,"a_Position"},
			{ShaderDataType::Float3,"a_Normal"},
			{ShaderDataType::Float2,"a_TexCoord"}
		}
	);
	UploadVertexData();
	m_VertexArray->AddVertexBuffer(m_VertexBuffer);
	m_VertexArray->AddIndexBuffer(m_IndexBuffer);
}
void Mesh::UploadVertexData()
{
	m_VertexBuffer->SetData(m_Vertices.data(), static_cast<uint32_t>(m_Vertices.size() * sizeof(Vertex)));
}

void Mesh::Draw() const
{
	// 用 IndexBuffer 记的个数，而不是 m_Indices.size()：
	// draw call 应当以 GPU 侧真实持有的数据为准，两者一旦不一致能立刻暴露出来
	//
	// 这里不用自己 Bind VAO：RenderCommand::DrawIndexed 内部会绑
	RenderCommand::DrawIndexed(*m_VertexArray, m_IndexBuffer->GetCount());
}
