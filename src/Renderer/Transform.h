#pragma once
#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>
class Transform
{
public:
    glm::mat4 GetModelMatrix() const 
    {

        glm::mat4 T = glm::translate(glm::mat4(1.0f), Position);

        glm::mat4 R = glm::eulerAngleXYZ(Rotation.x, Rotation.y, Rotation.z);

        glm::mat4 S = glm::scale(glm::mat4(1.0f), Scale);

        return T*R*S;
    }
    glm::mat3 GetNormalMatrix() const
    {
        return glm::transpose(glm::inverse(glm::mat3(GetModelMatrix())));
    }

public:
	glm::vec3 Position{ 0.0f };
	glm::vec3 Rotation{ 0.0f };
	glm::vec3 Scale {1.0f,1.0f,1.0f};
};