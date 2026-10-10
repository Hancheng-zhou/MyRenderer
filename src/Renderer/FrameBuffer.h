#pragma once

#include <cstdint>

// ===========================================================================
//  ZHC - Renderer/FrameBuffer.h
//
//  离屏渲染目标：颜色与深度都挂在【纹理】上，而不是 renderbuffer。
//
//  为什么用纹理（而不是 renderbuffer）：
//    renderbuffer 只能当附件，不能被采样。纹理版的附件可以直接
//    贴进 ImGui 窗口显示，深度附件以后还能做阴影贴图、SSAO、
//    深度可视化——这些全都要采样深度。一开始就用纹理，后面不用返工。
//
//  职责边界：
//    管：FBO 的创建 / 重建 / 释放，以及附件的纹理 ID
//    不管：视口。绑定之后请自己调
//          RenderCommand::SetViewport(0, 0, GetWidth(), GetHeight())
//          因为视口是全局状态，散在类里容易和别处的设置打架。
//
//  生命周期约束（和 Window 的规则类似）：
//    FrameBuffer 析构时会调 glDeleteFramebuffers，所以它必须在
//    Window 之前析构 —— 在 main 里声明在 window 之后就满足。
// ===========================================================================
enum class TextureFormat
{
	RGBA8,//最终显示用
	RGBA16F// HDR 中间缓冲用
};
struct FrameBufferSpec
{
	uint32_t Width = 1;
	uint32_t Height = 1;
	bool DepthOnly = false;
	TextureFormat ColorFormat = TextureFormat::RGBA8;
};
class FrameBuffer
{
public:
	explicit FrameBuffer(FrameBufferSpec spec);
	~FrameBuffer();

	// GL 句柄不适合复制：两个对象持同一批 ID，析构时会重复删除
	FrameBuffer(const FrameBuffer&) = delete;
	FrameBuffer& operator=(const FrameBuffer&) = delete;

	void Bind() const;    // 绑定到 GL_FRAMEBUFFER，之后的绘制都进这里
	void Unbind() const;  // 绑回 0，也就是默认帧缓冲（屏幕）

	// 尺寸变化时重建。尺寸没变（或传 0，比如窗口最小化）则什么都不做
	void Resize(uint32_t width, uint32_t height);

	uint32_t GetWidth()  const { return m_Spec.Width; }
	uint32_t GetHeight() const { return m_Spec.Height; }
	bool IsDepthOnly() const { return m_Spec.DepthOnly; }

	// 纹理 ID：颜色附件直接给 ImGui::Image 用，以后也能给后处理 pass 采样
	uint32_t GetColorAttachmentID()        const { return m_ColorAttachment; }
	uint32_t GetDepthAttachmentID() const { return m_DepthAttachment; }

private:
	void Invalidate();  // 创建 / 重建全部 GL 对象
	void Release();     // 释放全部 GL 对象，并把 ID 归零

private:
	FrameBufferSpec m_Spec;
	uint32_t m_RendererID = 0;
	uint32_t m_ColorAttachment = 0;
	uint32_t m_DepthAttachment = 0;
	
};
