#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "Renderer/RenderSettings.h"
#include "Core/Sampling.h"
class Scene;
class FrameBuffer;
class Shader;
struct Light;   // 注意是 struct 不是 class：写错会触发 C4099（前后不一致）

// ===========================================================================
//  ZHC - Renderer/SceneRenderer.h
//
//  "把一整个场景画出来"这件事的可复用单元。
//
//  为什么值得单独成类（而不是继续写在 main 的循环里）：
//    阴影贴图需要"从光源的角度把场景再画一遍"，后处理需要"全屏再画一遍"。
//    有了这个单元，那两件事各是加一个函数；没有它，只能在 main 里
//    复制粘贴第二份渲染循环 —— 然后两份会开始逐渐不同步，这是最难查的一类 bug。
//
//  职责边界：
//    管：绑定渲染目标、清屏、上传"整场共享"的 uniform（相机 / 光源）、
//        逐个物体提交绘制
//    不管：场景数据的更新（物体自转之类，那是 UpdateSceneAnimation 的事）、
//          面板、窗口、输入
//
//  命名说明：
//    叫 SceneRenderer 而不是 Renderer，是为了不和 RenderCommand
//    （更低一层的 GL 命令封装）混淆
//
//  为什么"数据更新"不在里面：
//    渲染阶段应该是【只读】的。一旦渲染时改数据，将来做阴影贴图
//    （一帧渲染两遍场景）就会出问题
// ===========================================================================
class SceneRenderer
{
public:
	void RenderShadowPass(const Scene& scene,
		const FrameBuffer& shadowMap, Shader & depthShader,
		const RenderSettings& settings);

	void RenderSceneByPCF(const Scene& scene,
	                 const FrameBuffer& target,
					const FrameBuffer& shadowMap,
	                 Shader& shader,
	                 const RenderSettings& settings);
	void RenderSceneByPCSS(const Scene& scene,
		const FrameBuffer& target,
		const FrameBuffer& shadowMap,
		Shader& shader,
		const RenderSettings& settings);
private:
	// ---- 内部实现 ----

	// 把 Scene 里的光源收集成 GPU 要的紧凑数组
	void PackLights(const Scene& scene);

	// 上传光源数组。必须在 shader.Bind() 之后调用
	void UploadLights(Shader& shader);
	const Light* FindShadowLight(const Scene& scene) const;
	glm::mat4 ComputeLightSpaceMatrix(const Light& light,
		const RenderSettings& settings) const;
	// -----------------------------------------------------------------------
	//  光源打包缓冲。
	//
	//  提成成员而不是每帧建局部 vector：容量跨帧复用，避免每帧重新分配。
	//  更重要的是它们是"渲染器自己的工作区"，不该出现在调用方眼里 ——
	//  这就是"状态跟着逻辑走"的另一半。
	//
	//  为什么要打包：
	//    Light 是"编辑友好"的松散结构（position 和 color 之间还隔着
	//    direction 等字段），不能直接当 vec3[] 传给 GPU
	//
	//  为什么拆成这么多平行数组：
	//    GLSL 侧就是一组平行数组，且 (c,l,q) 打包成 vec3、
	//    (inner,outer) 打包成 vec2 之后，上传次数与灯数无关
	// -----------------------------------------------------------------------
	std::vector<int>       m_LightTypes;
	std::vector<glm::vec3> m_LightPositions;
	std::vector<glm::vec3> m_LightDirections;
	std::vector<glm::vec3> m_LightColors;
	std::vector<float>     m_LightIntensities;
	std::vector<glm::vec3> m_LightAttenuations;   // (constant, linear, quadratic)
	std::vector<glm::vec2> m_LightCutoffs;        // (innerCos, outerCos)
	// "做阴影的那盏灯"在【打包后数组】里的下标，由 PackLights 每帧填。
	// -1 = 这一帧没有灯要求投影。
	//
	// 【为什么不能直接用 scene.GetLights() 里的下标】
	//   PackLights 会把 enabled == false 的灯跳掉，所以打包后的下标
	//   和场景里的下标可能不一致 —— 而着色器里比的是打包后的下标
	int m_ShadowLightIndex = -1;
	//Poisson disk sampling 硬编码 注意 32 个样本点必须与.glsl文件里一致
	const int m_PoissonDiskSampleCount = 32;
	const std::vector<glm::vec2> m_PoissonDisk = {
	glm::vec2(-0.613392f,  0.617481f),
	glm::vec2(0.170019f, -0.040254f),
	glm::vec2(-0.299417f,  0.791925f),
	glm::vec2(0.645680f,  0.493210f),
	glm::vec2(-0.651784f,  0.717887f),
	glm::vec2(0.421003f,  0.027070f),
	glm::vec2(-0.817194f, -0.271096f),
	glm::vec2(-0.705374f, -0.668203f),
	glm::vec2(0.977050f, -0.108615f),
	glm::vec2(0.063326f,  0.142369f),
	glm::vec2(0.203528f,  0.214331f),
	glm::vec2(-0.667531f,  0.326090f),
	glm::vec2(-0.098422f, -0.295755f),
	glm::vec2(-0.885922f,  0.215369f),
	glm::vec2(0.566637f,  0.605213f),
	glm::vec2(0.039766f, -0.396100f),
	glm::vec2(0.751946f,  0.453352f),
	glm::vec2(0.078707f, -0.715323f),
	glm::vec2(-0.075838f, -0.529344f),
	glm::vec2(0.724479f, -0.580798f),
	glm::vec2(0.222999f, -0.215125f),
	glm::vec2(-0.467574f, -0.405438f),
	glm::vec2(-0.248268f, -0.814753f),
	glm::vec2(0.354411f, -0.887570f),
	glm::vec2(0.175817f,  0.382366f),
	glm::vec2(0.487472f, -0.063082f),
	glm::vec2(-0.084078f,  0.898312f),
	glm::vec2(0.488876f, -0.783441f),
	glm::vec2(0.470016f,  0.217933f),
	glm::vec2(-0.696890f, -0.549791f),
	glm::vec2(-0.149693f,  0.605762f),
	glm::vec2(0.034211f,  0.979980f)
	};
};
