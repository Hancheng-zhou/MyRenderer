#include "FrameBuffer.h"

#include <glad/glad.h>
#include <stdexcept>
#include <string>   // std::to_string（<stdexcept> 不保证会带进来）

FrameBuffer::FrameBuffer(uint32_t width, uint32_t height)
	: m_Width(width)
	, m_Height(height)
{
	Invalidate();
}

FrameBuffer::~FrameBuffer()
{
	Release();
}

void FrameBuffer::Bind() const
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
}

void FrameBuffer::Unbind() const
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffer::Resize(uint32_t width, uint32_t height)
{
	// width / height 为 0 是"窗口被最小化"，不是真的想渲染到 0 像素：
	// 用 0 去调 glTexImage2D 会得到一个永远不完整的 FBO，
	// 所以这里直接拒绝，等窗口恢复后自然会被再次调用
	if (width == 0 || height == 0)
	{
		return;
	}
	if (width == m_Width && height == m_Height)
	{
		return;
	}

	m_Width  = width;
	m_Height = height;
	Invalidate();
}

void FrameBuffer::Invalidate()
{
	// 重建前先把旧的删干净，否则每次 resize 都会泄漏一组纹理
	if (m_RendererID != 0)
	{
		Release();
	}

	glGenFramebuffers(1, &m_RendererID);
	glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

	// ---- 颜色附件 ----
	glGenTextures(1, &m_ColorAttachment);
	glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height),
	             0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// CLAMP_TO_EDGE：显示尺寸和渲染尺寸不一致时，UV 会在边缘做插值，
	// 用 REPEAT 会在边框上采到对侧的像素（表现为画面边缘有一条杂色）
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

	// ---- 深度 + 模板附件（合并成一张纹理）----
	glGenTextures(1, &m_DepthStencilAttachment);
	glBindTexture(GL_TEXTURE_2D, m_DepthStencilAttachment);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height),
	             0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
	// 深度用 NEAREST：插值出来的深度值没有物理意义
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthStencilAttachment, 0);

	// FBO 不完整也是一种"运行时才会暴露"的错误：屏幕上看是黑屏，
	// 排查成本极高，所以在这里就检查并抛出可读信息
	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		// 构造函数抛异常时析构函数不会执行，所以必须自己把已经创建的对象清掉，
		// 否则这组 FBO / 纹理会泄漏（与 Window 构造函数里对 glfwInit 的处理同理）
		Release();
		throw std::runtime_error("[FrameBuffer] framebuffer is incomplete, status = " + std::to_string(status));
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffer::Release()
{
	if (m_DepthStencilAttachment != 0)
	{
		glDeleteTextures(1, &m_DepthStencilAttachment);
		m_DepthStencilAttachment = 0;
	}
	if (m_ColorAttachment != 0)
	{
		glDeleteTextures(1, &m_ColorAttachment);
		m_ColorAttachment = 0;
	}
	if (m_RendererID != 0)
	{
		glDeleteFramebuffers(1, &m_RendererID);
		m_RendererID = 0;
	}
}
