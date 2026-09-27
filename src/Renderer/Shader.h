#pragma once
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <memory>
class Shader
{
public:
	Shader(const std::string& filepath);
	~Shader();
	Shader(const Shader&) = delete;
	Shader& operator=(const Shader&) = delete;
	void Bind() const;
	void Unbind() const;
	void SetInt(const std::string& name, int value);
	void SetIntArray(const std::string& name, const int* values, uint32_t count);
	void SetFloat(const std::string& name, float value);
	void SetFloatArray(const std::string& name, const float* values, uint32_t count);
	void SetFloat2(const std::string& name, const glm::vec2& value);
	void SetFloat2Array(const std::string& name, const glm::vec2* values, uint32_t count);
	void SetFloat3(const std::string& name, const glm::vec3& value);
	void SetFloat3Array(const std::string& name, const glm::vec3* values, uint32_t count);
	void SetFloat4(const std::string& name, const glm::vec4& value);
	void SetMat3(const std::string& name, const glm::mat3& value);
	void SetMat4(const std::string& name, const glm::mat4& value);
	const std::string GetName() const{ return m_Name; }
	const uint32_t GetRendererID() const { return m_RendererID; }

	std::string ReadFile(const std::string& filepath);
	std::unordered_map<uint32_t, std::string> PreProcess(const std::string& source);
	void Compile(const std::unordered_map<uint32_t, std::string>& shaderSources);
private:
	// 查 uniform location：先查缓存，未命中才去问驱动，并把结果记下来
	uint32_t GetUniformLocation(const std::string& name);

	uint32_t m_RendererID=0;
	std::string m_Name;
	std::unordered_map<std::string, uint32_t> m_LocationCache;
};

class ShaderLibrary
{
public:
	void Add(const std::string& name, const std::shared_ptr<Shader>& shader);
	void Add(const std::shared_ptr<Shader> shader);
	std::shared_ptr<Shader> Load(const std::string& filepath);
	std::shared_ptr<Shader> Load(const std::string& name, const std::string& filepath);

	std::shared_ptr<Shader> Get(const std::string& name);
	bool Exists(const std::string& name) const;
private:
	std::unordered_map<std::string, std::shared_ptr<Shader>> m_Shaders;
};