#include "Scene.h"

#include <utility>   // std::move

Scene::Scene(const glm::vec3& cameraPosition,
             const glm::vec3& cameraTarget,
             float fovDegrees,
             float aspectRatio,
             float nearZ,
             float farZ)
	: m_Camera(cameraPosition, cameraTarget, fovDegrees, aspectRatio, nearZ, farZ)
{
}

RenderObject& Scene::AddObject(RenderObject object)
{
	m_RenderObjects.push_back(std::move(object));
	return m_RenderObjects.back();
}

Light& Scene::AddLight(Light light)
{
	m_Lights.push_back(std::move(light));
	return m_Lights.back();
}
