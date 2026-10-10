#pragma once
#include "Transform.h"
#include "Mesh.h"
#include "Material.h"
#include "Core/PrimitiveDesc.h"
#include <string>

// 场景里的一个可见对象 = 几何 + 外观 + 位姿。
//
// 注意它【不持有】"怎么画"：没有 shader、没有 draw 调用。
// 渲染器来读它，它不知道渲染器的存在
struct RenderObject
{
	std::string Name;                 // 面板/调试用（下拉框显示的是它）
	std::shared_ptr<Mesh> Geometry;   // 几何（可以是 CPU 顶点，也可以是已上传的 GPU 资源）
	Material              Mat;        // 外观
	Transform             Xform;      // 在世界中的位姿
	bool IsVisible = true;

	// ---- 程序化几何：两份配方成对存在 ----
	//
	// Primitive      = 【想要的】配方（面板在改这一份）
	// BuiltPrimitive = 【已经生成的】配方（当前 Geometry 实际是照它建的）
	//
	// 【为什么非要存两份】两者可以在时间上错开：面板里改了参数、还没点重建。
	// 只有一份的话，面板就无从判断"几何是否已经跟上参数"，
	// 只能每次点击都盲重建一份可能一模一样的几何 ——
	// 表现就是"Rebuild 按钮点了没反应"，而那其实不是 bug，是它真的没事可做
	PrimitiveDesc Primitive;
	PrimitiveDesc BuiltPrimitive;

	// 配方改了但还没应用到几何上（没有几何也算）。
	//
	// 判断收在这里，不让面板自己拼条件 ——
	// 否则"什么算待重建"会有两份定义，早晚不一致
	bool IsGeometryPending() const
	{
		return Geometry == nullptr || Primitive != BuiltPrimitive;
	}

	// 场景初始化用：挂上一份【外面建好、可能被多个物体共享】的几何，
	// 同时把配方标成"已同步"。
	//
	// 【为什么不直接写 Geometry = mesh】那个赋值不带配方，
	// 物体就会一直保留默认配方（Cube 1.0），于是面板显示"up to date"
	// 却和真实几何不符 —— 一个不报错的谎。
	// 让"几何"和"配方"从同一个 desc 出来，这类错位就不可能发生
	void SetSharedGeometry(const std::shared_ptr<Mesh>& geometry, const PrimitiveDesc& desc)
	{
		Geometry       = geometry;
		Primitive      = desc;
		BuiltPrimitive = desc;
	}

	// 按 Primitive 这份配方重建几何。
	//
	// 【为什么把它做成成员函数，而不是让调用方自己写两行】
	// 重建必须同时做两件事：换 Geometry、记 BuiltPrimitive。
	// 只做前一件，面板就会永远显示"已同步"，你再也看不到"待应用"提示。
	// 把这个不变量收在一个地方，就没人能漏掉后一件
	//
	// 【注意会打断共享】原来几个物体可能指向同一个 Mesh，
	// 重建之后这个物体就单独持有一份了。旧的那份如果没别人引用会立刻析构
	//（shared_ptr 归零 → 析构 → glDelete 那组 VAO/VBO/EBO）
	void RebuildGeometry()
	{
		Geometry       = std::make_shared<Mesh>(Mesh::Create(Primitive));
		BuiltPrimitive = Primitive;
	}
};
