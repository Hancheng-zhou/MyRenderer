#pragma once

#include <cstdint>

// ===========================================================================
//  ZHC - Core/PrimitiveDesc.h
//
//  "程序化几何的配方"：生成什么形状 + 按什么参数生成。
//
//  为什么要有这个文件：
//    Mesh 里是【烘焙好的顶点】，反推不出"当初是按什么参数生成的"。
//    而"这个物体是什么形状、多大"确实属于物体自己的属性 ——
//    面板靠它实现"改参数 → 重建几何"。
//
//  【和 Transform::Scale 的区别，很容易混】
//    Transform::Scale 是【变换】：几何数据不变，在顶点着色器里被缩放，
//                                非均匀缩放还需要逆转置矩阵修正法线
//    PrimitiveDesc    是【几何本身】：顶点数据真的被重新生成
//
//  依赖方向：本文件不认识 GL，也不认识 Mesh、Scene、Renderer ——
//  它只是"一份配方"，谁想生成几何谁自己来读
// ===========================================================================

// 几何的种类。
//
// 【为什么必须有它】只有尺寸参数是半份设计：面板不知道该照着哪个生成
// 函数去重建。少了这个枚举，"Rebuild geometry" 会把一个球重新生成成
// 立方体，而且什么提示都没有
enum class PrimitiveType
{
	Cube = 0,
	Sphere = 1,
	Obj =2,
};

// 枚举值 → 显示名。
//
// 直接以数组形式给出，因为 ImGui 的 Combo 要的就是 const char* 数组。
// 【下标必须等于枚举值】—— 加形状时枚举和这个数组一起改，
// 而且都在这一个文件里，不会漏掉另一个文件
inline constexpr const char* k_PrimitiveTypeNames[] = { "Cube", "Sphere","Obj"};
inline constexpr int         k_PrimitiveTypeCount   = 3;

// 越界安全的取名。枚举和数组哪天不同步了，宁可显示 "Unknown"，
// 也不要拿越界下标去读内存
inline const char* PrimitiveTypeName(PrimitiveType type)
{
	const int index = static_cast<int>(type);
	return (index >= 0 && index < k_PrimitiveTypeCount)
		? k_PrimitiveTypeNames[index]
		: "Unknown";
}

// 一份几何配方。
//
// 【为什么收成一个结构体，而不是摊成 RenderObject 的五个字段】
//   1. "参数是否已经应用到几何上"变成一次结构比较，
//      以后加形状也不用再加一对 xxx / BuiltXxx 字段
//   2. 场景要序列化时，这块天然就是一个字段
//   3. 参数和类型绑在一起传，函数签名不会一路膨胀
struct PrimitiveDesc
{
	PrimitiveType Type = PrimitiveType::Cube;

	float Size = 1.0f;   // Cube: 边长      Sphere: 半径

	// 下面两个只对 Sphere 有意义，但刻意留在同一个结构里：
	// 从球切回立方体再切回来时，之前调好的细分级别还在
	uint32_t Segments = 32;   // 经线数：绕 Y 轴一圈切多少份
	uint32_t Rings    = 16;   // 纬线数：从北极到南极切多少份

	std::string Path;
	// 【这里用浮点 == 是安全的】这些值只由面板的同一个控件写入，
	// 不参与任何累加/插值运算，不存在"数学上应相等但浮点上不等"的情况。
	// 换成"重建后算出来的尺寸"再比较就不能这么写了
	bool operator==(const PrimitiveDesc& other) const
	{
		return Type == other.Type
			&& Size == other.Size
			&& Segments == other.Segments
			&& Rings == other.Rings
			&& Path ==other.Path;
	}

	bool operator!=(const PrimitiveDesc& other) const { return !(*this == other); }
};
