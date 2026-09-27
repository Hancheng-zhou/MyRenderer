#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// ===========================================================================
//  ZHC - Renderer/Camera.h
//
//  职责边界：
//    管：相机姿态（position + yaw/pitch）、view 矩阵、projection 矩阵
//    不管：窗口与输入。按键/鼠标由 main 读取后调用 Move/Rotate/Zoom，
//          这样 Camera 不依赖 GLFW，换输入源（手柄/脚本/回放）时不用动它。
//
//  为什么内部存 yaw/pitch 而不是 target 点：
//    视角控制天然是增量式的（鼠标拖动 = 角度增量），yaw/pitch 直接加减；
//    若内部存 target 点，每次旋转都要先算出方向再反推 target，来回转换
//    既别扭又容易积累误差。构造时收 target 只是为了调用方便，进来就转换。
//
//  坐标系约定（GLM / OpenGL 默认，右手系）：
//    +X 向右，+Y 向上，初始视线朝 -Z（yaw = -90°）
//    yaw   绕世界 Y 轴，向右转为正
//    pitch 俯仰，抬头为正，夹紧在 ±k_MaxPitch 内
// ===========================================================================

// ---------------------------------------------------------------------------
//  相机状态快照：描述一个"观察条件"的完整数值
//
//  用途是【复现】——学习渲染器时经常要对比"开阴影 / 关阴影"这类差异，
//  如果两次运行的观察角度不同，对比就失去意义。把状态存下来再恢复，
//  就能保证除被对比的那个变量之外，其它条件完全一致。
//
//  刻意不含宽高比：那属于窗口属性，不该被视角预设覆盖
//  （否则拉动窗口后恢复视角，画面比例会被一起改回去）。
// ---------------------------------------------------------------------------
struct CameraState
{
	glm::vec3 Position{ 0.0f };
	float     Yaw        = -90.0f;   // 度
	float     Pitch      = 0.0f;     // 度
	float     FovDegrees = 45.0f;
	float     NearZ      = 0.1f;
	float     FarZ       = 100.0f;

	// 由「站在 position 看向 target」直接得到状态，省得手动算 yaw/pitch。
	// 俯视 / 仰视等极端角度会自动夹紧 pitch
	static CameraState FromLookAt(const glm::vec3& position,
	                              const glm::vec3& target,
	                              float fovDegrees = 45.0f,
	                              float nearZ = 0.1f,
	                              float farZ = 100.0f);
};

class Camera
{
public:
	// pitch 必须夹紧：越接近 ±90°，视线越平行于世界上方向，
	// glm::lookAt 的 up 参考向量就越退化，画面会翻转或抖动
	static constexpr float k_MaxPitch = 89.0f;

	// FOV 上下限，防止滚轮把视锥推到退化（0° 与 180° 都不合法）
	static constexpr float k_MinFov = 1.0f;
	static constexpr float k_MaxFov = 120.0f;

	// target 只用于确定初始朝向，之后由 yaw/pitch 接管
	Camera(const glm::vec3& position,
	       const glm::vec3& target,
	       float fovDegrees,
	       float aspectRatio,
	       float nearZ = 0.1f,
	       float farZ = 100.0f);

	// ---- 状态修改：由 main 读输入后调用 ----
	void SetPosition(const glm::vec3& position);

	// 沿相机自身的前 / 右方向平移
	void MoveForward(float distance);
	void MoveRight(float distance);
	// 沿【世界】Y 轴升降：低头时按"上升"应该是垂直往上走，而不是斜着飞
	void MoveUp(float distance);
	// 世界空间任意位移（传送、跟随等场景用）
	void Move(const glm::vec3& worldOffset);

	void Rotate(float deltaYawDegrees, float deltaPitchDegrees);
	void Zoom(float deltaFovDegrees);

	// 保持当前位置，转向看向 target（等价于重新指定一次注视点）
	void LookAt(const glm::vec3& target);
	// 直接指定朝向角（度），不做增量叠加
	void SetOrientation(float yawDegrees, float pitchDegrees);

	void SetFovDegrees(float fovDegrees);
	// near 必须 > 0 且 far > near，非法组合会被忽略而不是悄悄弄坏投影矩阵。
	// 这两个值是观察深度精度 / z-fighting 的主要旋钮
	void SetClipPlanes(float nearZ, float farZ);

	// 窗口尺寸变化时调用。内部只在宽高比真的变了时才重算投影矩阵
	void SetViewportSize(uint32_t width, uint32_t height);
	void SetAspectRatio(float aspectRatio);

	// ---- 状态快照：视角预设 / 效果对比用 ----
	CameraState GetState() const;
	void SetState(const CameraState& state);

	// ---- 只读查询 ----
	const glm::mat4& GetViewMatrix() const { return m_View; }
	const glm::mat4& GetProjectionMatrix() const { return m_Projection; }
	glm::mat4 GetViewProjection() const { return m_Projection * m_View; }

	const glm::vec3& GetPosition() const { return m_Position; }
	const glm::vec3& GetForward()  const { return m_Forward; }
	const glm::vec3& GetRight()    const { return m_Right; }
	const glm::vec3& GetUp()       const { return m_Up; }
	float GetYaw()         const { return m_Yaw; }
	float GetPitch()       const { return m_Pitch; }
	float GetFovDegrees()  const { return m_FovDegrees; }
	float GetAspectRatio() const { return m_AspectRatio; }
	float GetNearZ()       const { return m_NearZ; }
	float GetFarZ()        const { return m_FarZ; }

private:
	// yaw/pitch -> forward/right/up，并顺带刷新 view 矩阵
	void RecalculateOrientation();
	void RecalculateView();
	void RecalculateProjection();

private:
	// 姿态
	glm::vec3 m_Position{ 0.0f };
	float m_Yaw   = -90.0f;   // 度，初始朝 -Z
	float m_Pitch = 0.0f;     // 度

	// 由 yaw/pitch 派生。缓存下来是因为平移要用到 forward/right，
	// 而重算一次要过三角函数，没必要在多个地方重复算
	glm::vec3 m_Forward{ 0.0f, 0.0f, -1.0f };
	glm::vec3 m_Right{ 1.0f, 0.0f, 0.0f };
	glm::vec3 m_Up{ 0.0f, 1.0f, 0.0f };

	// 投影参数
	float m_FovDegrees  = 45.0f;
	float m_AspectRatio = 16.0f / 9.0f;
	float m_NearZ = 0.1f;
	float m_FarZ  = 100.0f;

	// 缓存矩阵：只在参数变化时重算，主循环里只读
	glm::mat4 m_View{ 1.0f };
	glm::mat4 m_Projection{ 1.0f };
};
