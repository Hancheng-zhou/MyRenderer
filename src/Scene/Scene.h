#pragma once

#include <cstddef>
#include <vector>
#include <glm/glm.hpp>

#include "Renderer/Camera.h"
#include "Renderer/RenderObject.h"
#include "Renderer/Light.h"

// ===========================================================================
//  ZHC - Scene/Scene.h
//
//  场景 = "这个世界里有什么"的完整描述。纯数据，不含任何 GL 调用。
//
//  依赖方向刻意是 Renderer -> Scene，而不是 Scene -> Renderer：
//  渲染器来读场景，场景不知道渲染器的存在。这样以后换后端
//  （延迟渲染、后处理、甚至软件光栅）时，场景数据一行都不用改。
//
//  职责边界：
//    管：相机、可见对象列表、光源列表
//    不管：怎么画（那是 Renderer 的事）、资源加载与 GL 对象生命周期
// ===========================================================================
class Scene
{
	
public:
	// 相机没有默认构造（位置 / 朝向 / 视场角都是必需的），所以 Scene 也不能默认构造。
	// 参数原样转发给 Camera —— "场景建出来的那一刻，观察条件就是确定的"
	Scene(const glm::vec3& cameraPosition,
	      const glm::vec3& cameraTarget,
	      float fovDegrees,
	      float aspectRatio,
	      float nearZ = 0.1f,
	      float farZ = 100.0f);

	// ---- 相机 ----
	// 刻意给两个版本：
	//   const    —— 渲染器只该"读"场景，拿到的相机不允许被改
	//   非 const —— WASD / 鼠标拖动必须能改相机
	const Camera& GetCamera() const { return m_Camera; }
	Camera&       GetCamera()       { return m_Camera; }

	// ---- 可见对象 ----
	const std::vector<RenderObject>& GetObjects() const { return m_RenderObjects; }
	RenderObject& GetObject(std::size_t index) { return m_RenderObjects[index]; }
	const RenderObject& GetObject(std::size_t index) const { return m_RenderObjects[index]; }
	RenderObject& AddObject(RenderObject object);

	// ---- 光源 ----
	const std::vector<Light>& GetLights() const { return m_Lights; }
	Light& GetLight(std::size_t index) { return m_Lights[index]; }
	const Light& GetLight(std::size_t index) const { return m_Lights[index]; }
	Light& AddLight(Light light);

	// ---- 统计：接到 ImGui 面板上非常直观 ----
	std::size_t GetObjectCount() const { return m_RenderObjects.size(); }
	std::size_t GetLightCount()  const { return m_Lights.size(); }

private:
	Camera                    m_Camera;
	std::vector<RenderObject> m_RenderObjects;
	std::vector<Light>        m_Lights;
public:
	static constexpr std::size_t k_MaxLightsCount = 8;
};
