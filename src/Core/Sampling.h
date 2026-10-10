#pragma once
#include <vector>
#include <random>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>

inline std::vector<glm::vec2> GeneratePoissonDiskSamples(
	int count, float minDistRatio = 0.8f, int k = 30)
// 生成 count 个落在单位圆盘内的 Poisson Disk 采样点
// minDistRatio: 最小距离相对于圆盘半径的比例，越小点越密。
// 典型值 0.7 ~ 0.9（针对想要的数量自动调）
// k: 每个活跃点的候选尝试次数，默认 30
{
	// 单位圆盘：半径 1，面积 PI
	// 想要大约 count 个点，每个点占据面积约 PI/count
	// 对于 Poisson Disk，最小距离 r 与点数 N 的关系近似：
	//   N ≈ (2 / sqrt(3)) * (R / r)^2   （六边形密排的渐近估计）
	// 反推：r ≈ R * sqrt( (2/sqrt(3)) / N )
	std::vector<glm::vec2> result;
	result.reserve(count);
	if (count <= 0)return result;
	const float R = 1.0f; // radius of the disk
	float minR = R * std::sqrt((2.0f / std::sqrt(3.0f)) / float(count));
	minR *= minDistRatio;
	float cellSize = minR / std::sqrt(2.0f);
	int gridWidth = static_cast<int>(std::ceil(2.0f * R / cellSize));
	std::vector<int> grid(gridWidth * gridWidth, -1);

	// 网格：格子边长 r / sqrt(2)，保证每格最多一个点
	auto gridIndex = [&](const glm::vec2& p)-> int {
		int gx = int((p.x + R) / cellSize);
		int gy = int((p.y + R) / cellSize);
		gx = glm::clamp(gx, 0, gridWidth - 1);
		gy = glm::clamp(gy, 0, gridWidth - 1);
		return gy * gridWidth + gx;
		};

	std::mt19937 rng(2002);
	std::uniform_real_distribution<float> dist(0.0f, 1.0f);

	// 均匀采样单位圆盘
	auto  randInUnitDisk = [&]() -> glm::vec2 {
		float angle = dist(rng) * 2.0f * glm::pi<float>();
		float rad = std::sqrt(dist(rng)) * R;
		return glm::vec2(rad * std::cos(angle), rad * std::sin(angle));
		};

	// 检查候选点 p 是否与已有点冲突
	auto isValid = [&](const glm::vec2& p) -> bool {
		// 1. 必须在单位圆盘内
		if (glm::dot(p, p) > R * R) return false;

		// 2. 检查周围 5x5 网格
		int gx = int((p.x + R) / cellSize);
		int gy = int((p.y + R) / cellSize);

		int xmin = std::max(gx - 2, 0);
		int xmax = std::min(gx + 2, gridWidth - 1);
		int ymin = std::max(gy - 2, 0);
		int ymax = std::min(gy + 2, gridWidth - 1);

		for (int y = ymin; y <= ymax; ++y)
			for (int x = xmin; x <= xmax; ++x)
			{
				int idx = grid[y * gridWidth + x];
				if (idx < 0) continue;
				glm::vec2 q = result[idx];
				if (glm::dot(p - q, p - q) < minR * minR) return false;
			}
		return true;
		};
	// 初始点：随机落在圆盘内
	glm::vec2 first = randInUnitDisk();
	result.push_back(first);
	grid[gridIndex(first)] = 0;

	std::vector<int> active;
	active.push_back(0);

	while (!active.empty() && int(result.size()) < count)
	{
		int activeIdx = int(dist(rng) * float(active.size()));
		if (activeIdx >= int(active.size())) activeIdx = int(active.size()) - 1;

		int pointIdx = active[activeIdx];
		glm::vec2 origin = result[pointIdx];

		bool found = false;
		for (int t = 0; t < k; ++t)
		{
			// 环形区域采样：半径 [r, 2r]
			float angle = dist(rng) * 2.0f * 3.14159265358979f;
			float rad = minR * (1.0f + dist(rng));
			glm::vec2 cand = origin + glm::vec2(rad * std::cos(angle),
				rad * std::sin(angle));

			if (!isValid(cand)) continue;

			int newIdx = int(result.size());
			result.push_back(cand);
			grid[gridIndex(cand)] = newIdx;
			active.push_back(newIdx);
			found = true;

			if (int(result.size()) >= count) break;
		}

		if (!found)
		{
			// 该活跃点周围再也放不下新点，移除
			active[activeIdx] = active.back();
			active.pop_back();
		}
	}

	// 如果数量还不够（极少见），用随机点补齐（会略微破坏最小距离）
	while (int(result.size()) < count)
	{
		result.push_back(randInUnitDisk());
	}

	// 如果超了，截断
	if (int(result.size()) > count)
		result.resize(count);

	return result;
}