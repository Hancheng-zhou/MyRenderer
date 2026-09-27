#include "Camera.h"

#include <algorithm>   // std::clamp
#include <cmath>       // std::cos, std::sin, std::atan2, std::asin, std::fmod

// <glm/glm.hpp> 只含核心类型与函数，lookAt / perspective 属于 gtc 扩展，
// 必须显式包含这个头，否则报"未声明的标识符"
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	// 世界上方向。刻意不使用相机的 up：pitch 变化时二者不再一致，
	// 用相机的 up 去推 forward 会自相矛盾
	const glm::vec3 k_WorldUp{ 0.0f, 1.0f, 0.0f };

	// position 与 target 重合时方向向量退化，长度平方小于此值就改用默认朝向
	constexpr float k_MinDirectionLengthSq = 1e-12f;

	// 方向向量 -> 欧拉角，与 RecalculateOrientation 中 forward 的公式互为逆运算：
	//   forward.x = cos(yaw)·cos(pitch)
	//   forward.y = sin(pitch)
	//   forward.z = sin(yaw)·cos(pitch)
	//
	// 输入必须已归一化。这里顺手把 pitch 夹紧到 ±k_MaxPitch：
	// 正上方 / 正下方的方向会让 forward 与世界上方向平行，
	// 之后算 right 时叉积退化成零向量，normalize 会产出 NaN。
	// 夹紧放在这里而不是交给调用方，是因为反解出极端角度的入口有好几个
	// （构造函数 / LookAt / CameraState::FromLookAt），漏一个就是难查的花屏。
	void DirectionToYawPitch(const glm::vec3& direction, float& outYawDegrees, float& outPitchDegrees)
	{
		outYawDegrees = glm::degrees(std::atan2(direction.z, direction.x));
		// asin 定义域是 [-1,1]，归一化引入的浮点误差可能让它越界并返回 NaN
		outPitchDegrees = std::clamp(
			glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f))),
			-Camera::k_MaxPitch, Camera::k_MaxPitch);
	}
}

// ---------------------------------------------------------------------------
//  CameraState
// ---------------------------------------------------------------------------

CameraState CameraState::FromLookAt(const glm::vec3& position,
                                    const glm::vec3& target,
                                    float fovDegrees,
                                    float nearZ,
                                    float farZ)
{
	CameraState state;
	state.Position   = position;
	state.FovDegrees = fovDegrees;
	state.NearZ      = nearZ;
	state.FarZ       = farZ;

	glm::vec3 direction = target - position;
	if (glm::dot(direction, direction) < k_MinDirectionLengthSq)
	{
		// 与目标重合时方向没有意义，退回默认朝向（-Z）
		direction = glm::vec3(0.0f, 0.0f, -1.0f);
	}
	DirectionToYawPitch(glm::normalize(direction), state.Yaw, state.Pitch);

	return state;
}

// ---------------------------------------------------------------------------
//  Camera
// ---------------------------------------------------------------------------

Camera::Camera(const glm::vec3& position,
               const glm::vec3& target,
               float fovDegrees,
               float aspectRatio,
               float nearZ,
               float farZ)
	: m_Position(position)
	, m_FovDegrees(std::clamp(fovDegrees, k_MinFov, k_MaxFov))
	, m_AspectRatio(aspectRatio > 0.0f ? aspectRatio : 1.0f)
	, m_NearZ(nearZ)
	, m_FarZ(farZ)
{
	// 由「看向 target」反解出内部的 yaw/pitch
	glm::vec3 direction = target - m_Position;
	if (glm::dot(direction, direction) < k_MinDirectionLengthSq)
	{
		direction = glm::vec3(0.0f, 0.0f, -1.0f);
	}
	DirectionToYawPitch(glm::normalize(direction), m_Yaw, m_Pitch);

	RecalculateOrientation();   // 内部会调用 RecalculateView()
	RecalculateProjection();
}

void Camera::SetPosition(const glm::vec3& position)
{
	m_Position = position;
	RecalculateView();
}

void Camera::MoveForward(float distance)
{
	if (distance == 0.0f)
	{
		return;   // 没有位移就不重算矩阵
	}
	m_Position += m_Forward * distance;
	RecalculateView();
}

void Camera::MoveRight(float distance)
{
	if (distance == 0.0f)
	{
		return;
	}
	m_Position += m_Right * distance;
	RecalculateView();
}

void Camera::MoveUp(float distance)
{
	if (distance == 0.0f)
	{
		return;
	}
	m_Position += k_WorldUp * distance;
	RecalculateView();
}

void Camera::Move(const glm::vec3& worldOffset)
{
	if (worldOffset == glm::vec3(0.0f))
	{
		return;
	}
	m_Position += worldOffset;
	RecalculateView();
}

void Camera::Rotate(float deltaYawDegrees, float deltaPitchDegrees)
{
	if (deltaYawDegrees == 0.0f && deltaPitchDegrees == 0.0f)
	{
		return;
	}

	m_Yaw += deltaYawDegrees;
	// 夹紧后 forward 不可能与 k_WorldUp 平行，RecalculateOrientation 里的叉积不会退化
	m_Pitch = std::clamp(m_Pitch + deltaPitchDegrees, -k_MaxPitch, k_MaxPitch);

	// 连续拖动会让 yaw 无限增长，长时间运行后 float 精度会下降
	m_Yaw = std::fmod(m_Yaw, 360.0f);

	RecalculateOrientation();
}

void Camera::LookAt(const glm::vec3& target)
{
	const glm::vec3 direction = target - m_Position;
	if (glm::dot(direction, direction) < k_MinDirectionLengthSq)
	{
		return;   // 目标与相机重合，朝向无意义，保持原样
	}

	DirectionToYawPitch(glm::normalize(direction), m_Yaw, m_Pitch);
	RecalculateOrientation();
}

void Camera::SetOrientation(float yawDegrees, float pitchDegrees)
{
	m_Yaw   = std::fmod(yawDegrees, 360.0f);
	m_Pitch = std::clamp(pitchDegrees, -k_MaxPitch, k_MaxPitch);
	RecalculateOrientation();
}

void Camera::Zoom(float deltaFovDegrees)
{
	SetFovDegrees(m_FovDegrees + deltaFovDegrees);
}

void Camera::SetFovDegrees(float fovDegrees)
{
	const float newFov = std::clamp(fovDegrees, k_MinFov, k_MaxFov);
	if (newFov == m_FovDegrees)
	{
		return;   // 已经顶到上下限，不必重算
	}
	m_FovDegrees = newFov;
	RecalculateProjection();
}

void Camera::SetClipPlanes(float nearZ, float farZ)
{
	// near 为 0 或负数会让投影矩阵除零，far <= near 会把视锥翻过来。
	// 这类非法参数直接忽略，而不是悄悄产出一个坏矩阵
	if (nearZ <= 0.0f || farZ <= nearZ)
	{
		return;
	}
	if (nearZ == m_NearZ && farZ == m_FarZ)
	{
		return;
	}

	m_NearZ = nearZ;
	m_FarZ  = farZ;
	RecalculateProjection();
}

void Camera::SetViewportSize(uint32_t width, uint32_t height)
{
	// 调用方必须传 framebuffer 尺寸（像素）而不是窗口尺寸（屏幕坐标）：
	// 高 DPI 屏上两者不相等，用错画面会被拉伸。
	// 窗口最小化时 height 会是 0，此时保留旧宽高比，避免除零
	SetAspectRatio(height > 0
		? static_cast<float>(width) / static_cast<float>(height)
		: m_AspectRatio);
}

void Camera::SetAspectRatio(float aspectRatio)
{
	if (aspectRatio <= 0.0f || aspectRatio == m_AspectRatio)
	{
		return;
	}
	m_AspectRatio = aspectRatio;
	RecalculateProjection();
}

CameraState Camera::GetState() const
{
	CameraState state;
	state.Position   = m_Position;
	state.Yaw        = m_Yaw;
	state.Pitch      = m_Pitch;
	state.FovDegrees = m_FovDegrees;
	state.NearZ      = m_NearZ;
	state.FarZ       = m_FarZ;
	return state;
}

void Camera::SetState(const CameraState& state)
{
	// 外部存下来的状态不保证合法（可能被手改过，也可能是老版本的数据），
	// 所以这里把每个量都过一遍和构造函数相同的约束，而不是直接赋值
	m_Position   = state.Position;
	m_Yaw        = std::fmod(state.Yaw, 360.0f);
	m_Pitch      = std::clamp(state.Pitch, -k_MaxPitch, k_MaxPitch);
	m_FovDegrees = std::clamp(state.FovDegrees, k_MinFov, k_MaxFov);

	if (state.NearZ > 0.0f && state.FarZ > state.NearZ)
	{
		m_NearZ = state.NearZ;
		m_FarZ  = state.FarZ;
	}

	RecalculateOrientation();   // 内含 RecalculateView
	RecalculateProjection();
}

void Camera::RecalculateOrientation()
{
	const float yawRad   = glm::radians(m_Yaw);
	const float pitchRad = glm::radians(m_Pitch);
	const float cosPitch = std::cos(pitchRad);

	// 球坐标 -> 笛卡尔。结果已经是单位向量
	// （cos²p·cos²y + sin²p + cos²p·sin²y = cos²p + sin²p = 1），不必再 normalize
	m_Forward = glm::vec3(std::cos(yawRad) * cosPitch,
	                      std::sin(pitchRad),
	                      std::sin(yawRad) * cosPitch);

	// pitch 被夹在 ±89°，forward 与 k_WorldUp 的夹角不会为 0，
	// 叉积长度不会退化，normalize 安全
	m_Right = glm::normalize(glm::cross(m_Forward, k_WorldUp));

	// forward 与 right 互相垂直且都是单位向量，叉积结果已经是单位向量
	m_Up = glm::cross(m_Right, m_Forward);

	RecalculateView();
}

void Camera::RecalculateView()
{
	// 传入的 up 已经是正交化过的 m_Up，因此它只用来定符号，不会引入倾斜
	m_View = glm::lookAt(m_Position, m_Position + m_Forward, m_Up);
}

void Camera::RecalculateProjection()
{
	// glm::perspective 默认就是右手系 + 深度 [-1,1]，与 OpenGL 的裁剪空间一致。
	// 不要定义 GLM_FORCE_DEPTH_ZERO_TO_ONE（那是 Vulkan）或
	// GLM_FORCE_LEFT_HANDED（那是 DirectX），否则画面会静默出错。
	// fovy 参数的单位是弧度，所以必须过一遍 glm::radians
	m_Projection = glm::perspective(glm::radians(m_FovDegrees), m_AspectRatio, m_NearZ, m_FarZ);
}
