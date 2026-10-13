#pragma once
#include <string>
#include <vector>
#include "Core/Vertex.h"

bool LoadObj(const std::string& filepath,
	std::vector<Vertex>& outVertices,
	std::vector<uint32_t>& outIndices,
	std::string* outError);
