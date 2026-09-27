#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

enum class LightType
{
	Directional = 0,
	Point = 1,
	Spot = 2
};

struct Light {
	LightType type = LightType::Point;
	glm::vec3 position{ 0.0f,0.0f,0.0f };
	glm::vec3 direction{ 0.0f,-1.0f,0.0f };
	glm::vec3 color{ 1.0f,1.0f,1.0f };
	float intensity = 1.0f;

	// 点光/聚光衰减
	float constant = 1.0f;
	float linear = 0.09f;
	float quadratic = 0.032f;

	// 聚光角度（存 cos 值，方便着色器直接比较）
	float innerCutoff = glm::cos(glm::radians(12.5f));
	float outerCutoff = glm::cos(glm::radians(17.5f));

	// 状态
	bool enabled = true;
	bool castShadow = false;
};