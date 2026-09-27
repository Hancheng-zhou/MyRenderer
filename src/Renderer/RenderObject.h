#pragma once
#include "Transform.h"
#include "Mesh.h"
#include "Material.h"

struct RenderObject
{
	std::shared_ptr<Mesh> Geometry;   //几何
	Material              Mat;        // 外观
	Transform             Xform;      // 在世界中的位姿
	bool IsVisible = true;
};