#include "Shader.h"
#include <unordered_map>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <array>
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace
{
	// "vertex" / "fragment" -> GL 的 shader 类型。
	// 类型名来自外部文件，写错了属于"资源内容错误"，不能用 assert：
	// Release 下 assert 会被预处理器整条删掉，错误会被静默吞掉。
	GLenum ShaderTypeFromString(const std::string& type)
	{
		if (type == "vertex")
			return GL_VERTEX_SHADER;
		if (type == "fragment" || type == "pixel")
			return GL_FRAGMENT_SHADER;

		throw std::runtime_error("Unknown shader type in #type declaration: '" + type + "'");
	}

	std::string ShaderTypeName(GLenum type)
	{
		switch (type)
		{
		case GL_VERTEX_SHADER:   return "vertex";
		case GL_FRAGMENT_SHADER: return "fragment";
		default:                 return "unknown";
		}
	}

	std::string GetShaderInfoLog(GLuint shader)
	{
		GLint maxLength = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
		std::vector<GLchar> infoLog(static_cast<size_t>(maxLength > 1 ? maxLength : 1), '\0');
		glGetShaderInfoLog(shader, maxLength, nullptr, infoLog.data());
		return std::string(infoLog.data());
	}

	std::string GetProgramInfoLog(GLuint program)
	{
		GLint maxLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
		std::vector<GLchar> infoLog(static_cast<size_t>(maxLength > 1 ? maxLength : 1), '\0');
		glGetProgramInfoLog(program, maxLength, nullptr, infoLog.data());
		return std::string(infoLog.data());
	}
}

Shader::Shader(const std::string& filepath)
{
	std::string source = ReadFile(filepath);
	auto shaderSources = PreProcess(source);
	Compile(shaderSources);
	// Extract name from filepath
	auto lastSlash = filepath.find_last_of("/\\");
	lastSlash = lastSlash == std::string::npos ? 0 : lastSlash + 1;
	auto lastDot = filepath.rfind('.');
	auto count = lastDot == std::string::npos ? filepath.size() - lastSlash : lastDot - lastSlash;
	m_Name = filepath.substr(lastSlash, count);
}
Shader::~Shader()
{
	if (m_RendererID != 0)
	{
		glDeleteProgram(m_RendererID);
		m_RendererID = 0;
	}
}
void Shader::Bind() const
{
	glUseProgram(m_RendererID);
}
void Shader::Unbind() const
{
	glUseProgram(0);
}

uint32_t Shader::GetUniformLocation(const std::string& name)
{
	// 【必须在查 location 之前把程序绑上】
	//
	// 这两件事在 GL 里是【不对称】的：
	//   glGetUniformLocation(program, name)  按【程序 ID】查，和当前状态无关
	//   glUniform*(location, value)          作用于【当前绑定的程序】
	//     （GL 规范原文：operates on the program object that was made part of
	//       current state by calling UseProgram）
	//
	// 也就是：location 是"按 A 程序查出来的"，value 却写进了"当前绑着的 B 程序"。
	// 只要调用方忘了先 Bind()，值就【静默地写到别的 shader 上】——
	// 你自己的 uniform 保持默认值 0，不报错、不警告。
	//
	// 【这个函数是所有 Set* 的唯一入口】（每个 Set* 第一件事就是调它），
	// 所以绑在这里等于一次修好全部 12 个 Set*，以后新增的也自动免疫。
	// 查 location 和写 value 本来就【必须作用在同一个程序上】，
	// 把这两件事绑在一起是语义上正确的，不是"给 getter 加副作用"
	glUseProgram(m_RendererID);

	// 命中缓存就直接返回，避免每次设 uniform 都做一次字符串查找
	const auto it = m_LocationCache.find(name);
	if (it != m_LocationCache.end())
	{
		return it->second;
	}

	// 未命中才去问驱动
	const GLint location = glGetUniformLocation(m_RendererID, name.c_str());
	if (location == -1)
	{
		// 常见原因：名字拼错，或者该 uniform 在着色器里没被用到（会被编译器优化掉）
		std::fprintf(stderr, "[Shader] uniform not found (unused or misspelled): %s\n", name.c_str());
	}

	// 失败结果也一并缓存：既避免反复问驱动，也避免每次都刷一遍警告
	m_LocationCache.emplace(name, location);
	return location;
}

void Shader::SetInt(const std::string& name, int value)
{
	glUniform1i(GetUniformLocation(name), value);
}

void Shader::SetIntArray(const std::string& name, const int* values, uint32_t count)
{
	glUniform1iv(GetUniformLocation(name), static_cast<GLsizei>(count), values);
}

void Shader::SetFloat(const std::string& name, float value)
{
	glUniform1f(GetUniformLocation(name), value);
}

void Shader::SetFloatArray(const std::string& name, const float* values, uint32_t count)
{
	glUniform1fv(GetUniformLocation(name), static_cast<GLsizei>(count), values);
}

void Shader::SetFloat2(const std::string& name, const glm::vec2& value)
{
	glUniform2f(GetUniformLocation(name), value.x, value.y);
}

void Shader::SetFloat2Array(const std::string& name, const glm::vec2* values, uint32_t count)
{
	glUniform2fv(GetUniformLocation(name), static_cast<GLsizei>(count),
		count > 0 ? glm::value_ptr(values[0]) : nullptr);
}

void Shader::SetFloat3(const std::string& name, const glm::vec3& value)
{
	glUniform3f(GetUniformLocation(name), value.x, value.y, value.z);
}

void Shader::SetFloat3Array(const std::string& name, const glm::vec3* values, uint32_t count)
{
	glUniform3fv(GetUniformLocation(name), static_cast<GLsizei>(count),
		count > 0 ? glm::value_ptr(values[0]) : nullptr);
}

void Shader::SetFloat4(const std::string& name, const glm::vec4& value)
{
	glUniform4f(GetUniformLocation(name), value.x, value.y, value.z, value.w);
}

void Shader::SetMat3(const std::string& name, const glm::mat3& value)
{
	glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::SetMat4(const std::string& name, const glm::mat4& value)
{
	glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

std::string Shader::ReadFile(const std::string& filepath)
{
	std::ifstream in(filepath, std::ios::in | std::ios::binary);
	if (!in.is_open())
	{
		// 最常见的原因就是工作目录不对，报错里直接点出来，省得自己猜文件名
		throw std::runtime_error("Cannot open shader file: " + filepath +
		                         " (working directory is probably not the repository root)");
	}

	in.seekg(0, std::ios::end);
	const std::streamsize size = in.tellg();
	if (size < 0)
	{
		throw std::runtime_error("Cannot determine size of shader file: " + filepath);
	}
	in.seekg(0, std::ios::beg);

	std::string result(static_cast<size_t>(size), '\0');
	if (size > 0)
	{
		in.read(result.data(), size);
	}
	in.close();

	return result;
}

std::unordered_map<GLenum, std::string> Shader::PreProcess(const std::string& source)
{
	std::unordered_map<GLenum, std::string> shaderSources;

	const char* typeToken = "#type";
	const size_t typeTokenLength = strlen(typeToken);
	size_t pos = source.find(typeToken, 0);

	while (pos != std::string::npos)
	{
		const size_t eol = source.find_first_of("\r\n", pos);
		if (eol == std::string::npos)
		{
			throw std::runtime_error("Shader source has a malformed #type line (no line break found)");
		}

		// 跳过 "#type" 与类型名之间的所有空白字符（空格和 Tab 都算），
		// 不要硬编码成"正好一个空格"，否则写成 "#type  vertex" 就解析失败
		const size_t typeBegin = source.find_first_not_of(" \t", pos + typeTokenLength);
		if (typeBegin == std::string::npos || typeBegin >= eol)
		{
			throw std::runtime_error("Shader source has an empty #type declaration");
		}

		// 同时去掉尾部空白，避免 "#type vertex  " 这种带尾随空格的写法
		size_t typeEnd = eol;
		while (typeEnd > typeBegin && (source[typeEnd - 1] == ' ' || source[typeEnd - 1] == '\t'))
		{
			--typeEnd;
		}

		const std::string type = source.substr(typeBegin, typeEnd - typeBegin);
		const GLenum shaderType = ShaderTypeFromString(type);

		// 下一个 #type 声明的位置
		const size_t nextLinePos = source.find_first_not_of("\r\n", eol);
		if (nextLinePos == std::string::npos)
		{
			throw std::runtime_error("Shader source ends right after a #type declaration");
		}
		pos = source.find(typeToken, nextLinePos);

		shaderSources[shaderType] = (pos == std::string::npos)
			? source.substr(nextLinePos)
			: source.substr(nextLinePos, pos - nextLinePos);
	}

	if (shaderSources.empty())
	{
		throw std::runtime_error("Shader source contains no #type declaration");
	}

	return shaderSources;
}

void Shader::Compile(const std::unordered_map<GLenum, std::string>& shaderSources)
{
	if (shaderSources.empty() || shaderSources.size() > 2)
	{
		throw std::runtime_error("Shader::Compile expects 1 or 2 stages, got " +
		                         std::to_string(shaderSources.size()));
	}

	const GLuint program = glCreateProgram();

	// 必须初始化，并且只按"实际创建成功的数量"清理：
	// 否则某个阶段编译失败时，会把数组里未初始化的句柄也当 shader 删掉
	std::array<GLuint, 2> glShaderIDs = { 0, 0 };
	int glShaderIDIndex = 0;

	for (const auto& kv : shaderSources)
	{
		const GLenum type = kv.first;
		const std::string& source = kv.second;

		const GLuint shader = glCreateShader(type);
		const GLchar* sourceCStr = source.c_str();
		glShaderSource(shader, 1, &sourceCStr, nullptr);
		glCompileShader(shader);

		GLint isCompiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			const std::string log = GetShaderInfoLog(shader);
			glDeleteShader(shader);

			// 把已经创建成功的 shader 也清掉，避免泄漏
			for (int i = 0; i < glShaderIDIndex; ++i)
			{
				glDeleteShader(glShaderIDs[i]);
			}
			glDeleteProgram(program);

			throw std::runtime_error("Failed to compile " + ShaderTypeName(type) + " shader:\n" + log);
		}

		glAttachShader(program, shader);
		glShaderIDs[glShaderIDIndex++] = shader;
	}

	glLinkProgram(program);

	GLint isLinked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
	if (isLinked == GL_FALSE)
	{
		const std::string log = GetProgramInfoLog(program);

		for (int i = 0; i < glShaderIDIndex; ++i)
		{
			glDetachShader(program, glShaderIDs[i]);
			glDeleteShader(glShaderIDs[i]);
		}
		glDeleteProgram(program);

		throw std::runtime_error("Failed to link shader program:\n" + log);
	}

	// 链接成功后 program 已持有编译结果，shader 对象可以释放
	for (int i = 0; i < glShaderIDIndex; ++i)
	{
		glDetachShader(program, glShaderIDs[i]);
		glDeleteShader(glShaderIDs[i]);
	}

	// 只在完全成功后才赋值，避免半成品句柄被析构函数当成有效 program 去删
	m_RendererID = program;
}

void ShaderLibrary::Add(const std::string& name, const std::shared_ptr<Shader>& shader)
{
	m_Shaders[name] = shader;
}

void ShaderLibrary::Add(const std::shared_ptr<Shader> shader)
{
	auto& name = shader->GetName();
	Add(name, shader);
}

std::shared_ptr<Shader> ShaderLibrary::Load(const std::string& filepath)
{

	auto shader = std::make_shared<Shader>(filepath);
	Add(shader);

	return shader;
}

std::shared_ptr<Shader> ShaderLibrary::Load(const std::string& name, const std::string& filepath)
{
	auto shader =std::make_shared<Shader>(filepath);
	Add(name, shader);
	return shader;
}

std::shared_ptr<Shader> ShaderLibrary::Get(const std::string& name)
{
	return m_Shaders[name];
}

bool ShaderLibrary::Exists(const std::string& name) const
{
	return m_Shaders.find(name) != m_Shaders.end();
}
