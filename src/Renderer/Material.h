#pragma once
#include <glm/glm.hpp>
#include <memory>
class Texture;
struct Material {
    glm::vec3 Ambient{ 0.1f, 0.1f, 0.1f };    // ka
    glm::vec3 Diffuse{ 0.8f, 0.8f, 0.8f };    // kd
    glm::vec3 Specular{ 0.5f, 0.5f, 0.5f };   // ks
    float Shininess = 32.0f;                // 高光指数
    std::shared_ptr<Texture> AlbedoTexture;
};