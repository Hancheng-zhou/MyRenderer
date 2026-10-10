#include "Mesh.h"
#include <algorithm>   // std::max（生成函数的参数下限保护）
#include <cmath>       // std::sin / std::cos
#include <utility>

// glm::pi<float>() 就在这里。别靠别的头文件间接带进来 ——
// gtc/constants 不属于 glm/glm.hpp，哪天上面那个间接来源被删掉就找不到符号了
#include <glm/gtc/constants.hpp>
#include "IndexBuffer.h"
#include "VertexBuffer.h"
#include "VertexArray.h"
#include "RenderCommand.h"
Mesh::Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices)
	:m_Vertices(std::move(vertices)),m_Indices(std::move(indices))
{
	SetupBuffers();
}

Mesh Mesh::Create(const PrimitiveDesc& desc)
{
	// 唯一的"配方 → 几何"分派点。
	//
	// 【为什么要有这一层】没有它，每个调用点都得自己 switch，
	// 于是"支持哪些形状"这件事会有 N 份副本 ——
	// 加一个球体要改 N 个地方，漏一个就是"某个界面里还是老形状"
	switch (desc.Type)
	{
	case PrimitiveType::Cube:
		return CreateCube(desc.Size);

	case PrimitiveType::Sphere:
		return CreateSphere(desc.Size, desc.Segments, desc.Rings);
	}

	// 兜底：枚举加了新值但这里忘了补 case（或者被塞进了越界值）。
	// 注意这里【不能】省 —— 走到函数末尾却没有 return 是未定义行为，
	// MSVC 只给一条警告，不会拦住编译
	return CreateCube(desc.Size);
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
Mesh Mesh::CreateSphere(float radius, uint32_t segments, uint32_t rings)
{
	// 下限保护：0 会除零，1 会退化成一条线段
	segments = std::max(segments, 3u);
	rings = std::max(rings, 2u);

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	vertices.reserve(static_cast<std::size_t>(segments + 1) * (rings + 1));
	indices.reserve(static_cast<std::size_t>(segments) * rings * 6);

	const float pi = glm::pi<float>();

	// ---- 顶点：纬线 × 经线 ----
	//
	// 【为什么是 segments+1 / rings+1 个】每个方向多出的那一圈和起始位置
	// 完全重合，但 u 分别是 0 和 1 —— 这是为了让 UV 接缝刚好落在"重复的那一圈"上。
	// 只生成 segments 个的话，最后一段的 u 会从 (segments-1)/segments
	// 直接插值回 0，贴图会被整条反向压缩，看起来像一道撕裂
	for (uint32_t y = 0; y <= rings; ++y)
	{
		const float v = static_cast<float>(y) / static_cast<float>(rings);
		const float phi = pi * v;              // 极角：0 = 北极(+Y)，pi = 南极(-Y)

		const float sinPhi = std::sin(phi);
		const float cosPhi = std::cos(phi);

		for (uint32_t x = 0; x <= segments; ++x)
		{
			const float u = static_cast<float>(x) / static_cast<float>(segments);
			const float theta = 2.0f * pi * u; // 方位角：绕 Y 轴一圈

			// 单位球上，位置方向就是法线 —— 省一次 normalize
			const glm::vec3 n(std::sin(theta) * sinPhi,
				cosPhi,
				std::cos(theta) * sinPhi);

			Vertex vertex;
			vertex.pos = n * radius;             // 球心在原点
			vertex.normal = n;
			vertex.texcoord = glm::vec2(u, 1.0f - v); // V 翻转，和 CreateCube 的约定一致
			vertices.push_back(vertex);
		}
	}

	// ---- 索引：相邻两圈拼一个四边形，拆成两个三角形 ----
	//
	// 【绕序要和 CreateCube 一致（CCW），否则背面剔除会吃掉半个球】
	// 推导：x 增加 = theta 增加；y 增加 = phi 增加（从北极往南走）
	//   ∂P/∂φ × ∂P/∂θ = sinφ · N   （N 是外法线，sinφ > 0）
	//   所以 a→c→b 的顺序（先走 φ 再走 θ）得到的法线朝向球外
	for (uint32_t y = 0; y < rings; ++y)
	{
		for (uint32_t x = 0; x < segments; ++x)
		{
			const uint32_t a = y * (segments + 1) + x;
			const uint32_t b = a + 1;
			const uint32_t c = a + (segments + 1);
			const uint32_t d = c + 1;

			indices.push_back(a); indices.push_back(c); indices.push_back(b);
			indices.push_back(b); indices.push_back(c); indices.push_back(d);
		}
	}

	return Mesh(std::move(vertices), std::move(indices));
}
void Mesh::SetupBuffers()
{
	// 【必须先把 VAO 绑上，再去创建 IndexBuffer】
	//
	// ELEMENT_ARRAY_BUFFER 的绑定属于【VAO 状态】，不是全局状态。
	// IndexBuffer 的构造函数里有一句
	//     glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
	// 此刻若绑着的是"别的 VAO"，这个新 EBO 就被记到了【那个】VAO 上，
	// 把它的索引缓冲悄悄换掉 —— 不报错，也没有任何提示。
	//
	// 启动阶段看不出问题：那时第一个 VAO 还没绑过，当前绑定恰好是 0，
	// glBindBuffer 只会记到一个不存在的 VAO 上，随后 AddIndexBuffer 会修正。
	// 但【运行期重建几何】时（面板的 Rebuild 按钮，场景里已有物体在画），
	// 当时绑着的正是上一个被绘制的物体 —— 它的 VAO 会被污染。
	// 现在各网格索引个数一致（都是 36，数据也相同）所以症状被掩盖了，
	// 一旦索引数不同就是"某个物体画错"甚至"引用了已删除的 EBO"
	m_VertexArray = std::make_shared<VertexArray>();
	m_VertexArray->Bind();

	m_VertexBuffer = std::make_shared<VertexBuffer>();
	m_VertexBuffer->SetLayout
	(
		{
			{ShaderDataType::Float3,"a_Position"},
			{ShaderDataType::Float3,"a_Normal"},
			{ShaderDataType::Float2,"a_TexCoord"}
		}
	);

	// 此时绑着的是自己的 VAO，新 EBO 会被正确记在它名下
	m_IndexBuffer = std::make_shared<IndexBuffer>(m_Indices.data(), GetIndexCount());

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
