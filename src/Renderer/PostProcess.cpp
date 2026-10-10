#include "Renderer/PostProcess.h"
#include "Renderer/FrameBuffer.h"
#include "Renderer/RenderCommand.h"

// 【文件名大小写必须和磁盘上完全一致】Windows/NTFS 不区分大小写，
// 写成 "Postprocess.glsl" 也能加载成功 —— 于是这个错在 Windows 上
// 永远不会暴露，直到换到 Linux / Android 才变成"shader 加载失败"。
// 而且那时你会去怀疑打包脚本，不会想到是这里的一个字母
PostProcess::PostProcess() :m_PostProcessShader("assets/shaders/PostProcess.glsl")
{
	// ---- 为什么这个 VAO 是空的 ----
	// 顶点在 vertex shader 里用 gl_VertexID 现算，几何数据一条都不需要。
	//
	// 但 VAO 【必须存在】：core profile 下 glDrawArrays 要求有一个非零的
	// VAO 绑着，否则 GL_INVALID_OPERATION —— 而且不报错、只是不画（黑屏）。
	// "没有属性"和"没有 VAO"是两回事，后者非法。
	//
	// VertexArray 的构造函数里已经做了 glGenVertexArrays，
	// 所以这一个成员就够了，一行额外代码都不用写

	// sampler uniform 是"从哪个纹理单元读"，它不随帧变化 ——
	// 设一次就够。（和 main.cpp 里 basicShader.SetInt("u_Texture", 0) 同理）
	//
	// 【名字必须和 shader 里声明的一字不差】写错了不会报错：
	// glGetUniformLocation 返回 -1，而 glUniform* 在 location = -1 上是
	// 【静默空操作】。更麻烦的是它"看起来能用"——sampler 没被设置时默认就是
	// 纹理单元 0，而 Render() 恰好把场景颜色绑在单元 0 上，于是画面完全正常。
	// 这种"靠巧合正确"的代码有个隐藏代价：哪天把它改绑到单元 1，立刻黑屏，
	// 而你会去怀疑 FBO、怀疑 shader，不会想到是这里少了一个字母
	m_PostProcessShader.SetInt("u_SceneTexture", 0);

}
void PostProcess::Render(const FrameBuffer& source, const FrameBuffer& target, const RenderSettings& settings)
{
	target.Bind();

	// 视口必须自己设 —— FrameBuffer.h 开头就写明了"管 FBO，不管视口"，
	// 因为视口是全局状态，散在类里会和别处的设置打架
	RenderCommand::SetViewport(0, 0,
		static_cast<int32_t>(target.GetWidth()),
		static_cast<int32_t>(target.GetHeight()));

	// ---- pass hygiene：全屏 pass 和深度没有任何关系 ----
	//
	// 【必须关，不能靠"它能过"】
	// 三角形在 z = 0，而深度清成 1.0，开着 GL_LESS 它也过得去 ——
	// 但那是靠巧合工作。全屏 pass 不该和深度比较发生关系。
	//
	// 【尤其是深度写入】不关的话会往 target 的深度附件里写一堆无意义的值，
	// 以后拿这张深度做 SSAO 或深度重建就会被污染
	RenderCommand::SetDepthTest(false);
	RenderCommand::SetDepthWrite(false);

	// 读 source，写 target —— 这是两个不同的 FBO，不会"同时读写同一张纹理"
	RenderCommand::BindTexture(source.GetColorAttachmentID(), 0);

	m_PostProcessShader.Bind();
	m_PostProcessShader.SetFloat("u_Exposure", settings.Exposure);
	m_PostProcessShader.SetInt("u_TonemapMode", settings.TonemapMode);
	m_PostProcessShader.SetInt("u_DebugHDRView", settings.DebugHDRView);
	// 3 个顶点，没有顶点缓冲 —— 全靠 gl_VertexID
	RenderCommand::DrawArrays(m_FullScreenVAO, 3);

	// ---- 把状态还回去 ----
	// 这两个开关是【全局】的。不恢复的话，下一帧的物体 pass 会带着
	// "不写深度"去画 —— 所有物体互相穿透，看起来像一团半透明的乱麻。
	// 而且症状离原因很远（你会去怀疑 Mesh、怀疑 shader）
	RenderCommand::SetDepthTest(true);
	RenderCommand::SetDepthWrite(true);

	m_PostProcessShader.Unbind();
	target.Unbind();
}