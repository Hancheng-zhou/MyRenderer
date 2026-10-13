#include "Core/ObjLoader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace 
{
	void ParseFaceVertex(const char* token, int& v, int& vt, int& vn) 
	{
		v = vt = vn = -1;
		const char* first = token;
		const char* slash_1 = std::strchr(first, '/');
		if (!slash_1)
		{
			v = static_cast<int>(std::strtol(first, nullptr, 10));
			return;
		}
		v = static_cast<int>(std::strtol(first, nullptr, 10));

		const char* second = slash_1 + 1;
		const char* slash_2 = std::strchr(second, '/');

		if (second!= slash_2)
		{
			vt = static_cast<int>(std::strtol(second, nullptr, 10));
		}
		if (slash_2)
		{
			vn = static_cast<int>(std::strtol(slash_2 + 1, nullptr, 10));
		}
	}
	int ResolveIndex(long raw, std::size_t count)
	{
		if (raw > 0)
		{
			const long index = raw - 1;
			return (index < static_cast<long>(count) ? static_cast<int>(index) : -1);
		}
		if (raw < 0)
		{
			const long index = static_cast<long>(count) + raw;   // -1 => count-1
			return (index >= 0) ? static_cast<int>(index) : -1;
		}
		return -1;
	}

	uint64_t PackKey(int v, int vt, int vn)
	{
		return (static_cast<uint64_t>(v+1)<<42)| (static_cast<uint64_t>(vt + 1) << 21)|static_cast<uint64_t>(vn + 1);
	}

}

bool LoadObj(const std::string& filepath, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices, std::string* outError)
{
	outVertices.clear();
	outIndices.clear();
	std::FILE* file = std::fopen(filepath.c_str(), "rb");
	if (!file)
	{
		if (outError) { *outError = "cannot open :" + filepath; }
		return false;
	}
	std::vector<glm::vec3> positions;
	std::vector<glm::vec2> texcoords;
	std::vector<glm::vec3> normals;
	std::unordered_map<uint32_t,uint32_t> uniqueVertex;
	char line[4096];
	long lineNumber = 0;
	while (std::fgets(line, sizeof(line), file))
	{
		++lineNumber;
		char* p = line;
		while (*p == ' ' || *p == '\t')
		{
			++p;
		}
		//------v : 位置
		if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t'))
		{
			glm::vec3 v(0.0f);
			std::sscanf(p + 1, "%f %f %f", &v.x, &v.y, &v.z);
			positions.push_back(v);
			continue;
		}
		// -----vt: UV
		if (p[0] == 'v' && p[1] == 't')
		{
			glm::vec2 t(0.0f);
			std::sscanf(p + 2, "%f %f", &t.x, &t.y);
			texcoords.push_back(t);
			continue;
		}
		// -----vn :法线
		if (p[0] == 'v' && p[1] == 'n')
		{
			glm::vec3 n(0.0f);
			std::sscanf(p + 2, "%f %f %f", &n.x, &n.y, &n.z);
			normals.push_back(n);
			continue;
		}
		// ------f: 面
		if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t'))
		{
			int face[64];
			int cornerCount = 0;

			// ---- 手写分词，【不用 strtok】----
			//
			// strtok 用的是全局静态游标：任何地方再调一次就会把它重置。
			// 而这个函数里有 fgets / sscanf / strchr 一堆调用，
			// 又是在一个循环里逐行处理 —— 一旦游标被碰，症状就是
			// "每行只解析出第一个字段"，也就是 indices 全空、verts 却正常。
			// 手写一个扫描器，状态全在局部变量里，没有这类风险
			const char* q = p + 1;
			while (*q && cornerCount < 64)
			{
				// 跳过字段之间的空白
				while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') { ++q; }
				if (*q == '\0') { break; }

				// 扫描一个字段：[begin, q)
				const char* begin = q;
				while (*q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n') { ++q; }

				// 【它不是以 '\0' 结尾的】—— 后面要用 strtol/strchr，
				// 那两个函数要求 NUL 结尾的字符串，所以先拷进一个小缓冲
				char field[64];
				const std::size_t length = static_cast<std::size_t>(q - begin);
				const std::size_t copyLength =
					(length < sizeof(field) - 1) ? length : sizeof(field) - 1;
				std::memcpy(field, begin, copyLength);
				field[copyLength] = '\0';

				int vi = 0, ti = 0, ni = 0;
				ParseFaceVertex(field, vi, ti, ni);

				const int rv = ResolveIndex(vi, positions.size());
				if (rv < 0)
				{
					std::fclose(file);
					if (outError)
					{
						*outError = "position index out of range at line "
							+ std::to_string(lineNumber);
					}
					return false;
				}
				const int rt = (ti >= 0) ? ResolveIndex(ti, texcoords.size()) : -1;
				const int rn = (ni >= 0) ? ResolveIndex(ni, normals.size()) : -1;

				// ---- 展开去重----
				const uint64_t key = PackKey(rv, rt, rn);
				auto it = uniqueVertex.find(key);
				if (it == uniqueVertex.end())
				{
					Vertex vertex;
					vertex.pos = positions[rv];
					vertex.texcoord = (rt >= 0) ? texcoords[rt] : glm::vec2(0.0f);
					vertex.normal = (rn >= 0) ? normals[rn] : glm::vec3(0.0f);

					const uint32_t newIndex = static_cast<uint32_t>(outVertices.size());
					uniqueVertex.emplace(key, newIndex);
					outVertices.push_back(vertex);
					face[cornerCount] = static_cast<int>(newIndex);
				}
				else
				{
					face[cornerCount] = static_cast<int>(it->second);
				}

				++cornerCount;
			}
			static int fLinesSeen = 0;
			if (fLinesSeen < 3)
			{
				std::printf("[obj] f line %ld -> cornerCount=%d\n", lineNumber, cornerCount);
				++fLinesSeen;
			}
			// ---- 扇形三角化----
			for (int i = 1; i + 1 < cornerCount; ++i)
			{
				outIndices.push_back(static_cast<uint32_t>(face[0]));
				outIndices.push_back(static_cast<uint32_t>(face[i]));
				outIndices.push_back(static_cast<uint32_t>(face[i + 1]));
			}
			continue;
		}


	}
	std::fclose(file);

	if (outVertices.empty() || outIndices.empty())
	{
		if (outError) { *outError = "no geometry found: " + filepath; }
		return false;
	}

	return true;
}
