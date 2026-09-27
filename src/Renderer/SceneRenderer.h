#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "Renderer/RenderSettings.h"

class Scene;
class FrameBuffer;
class Shader;

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
	void RenderScene(const Scene& scene,
	                 const FrameBuffer& target,
	                 Shader& shader,
	                 const RenderSettings& settings);

private:
	// ---- 内部实现 ----

	// 把 Scene 里的光源收集成 GPU 要的紧凑数组
	void PackLights(const Scene& scene);

	// 上传光源数组。必须在 shader.Bind() 之后调用
	void UploadLights(Shader& shader);

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
};
