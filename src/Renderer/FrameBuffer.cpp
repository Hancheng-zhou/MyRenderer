#include "FrameBuffer.h"

#include <glad/glad.h>
#include <stdexcept>
#include <string>   // std::to_string（<stdexcept> 不保证会带进来）


namespace
{
	// 深度纹理的采样参数。
	//
	// 【为什么 WRAP 用 CLAMP_TO_BORDER 而不是 CLAMP_TO_EDGE】
	//   这是为"深度纹理被采样"准备的（阴影贴图就是）。
	//   查询坐标落在 [0,1] 之外时，CLAMP_TO_EDGE 会采到【边缘像素】——
	//   而那个像素的深度值对于一次越界的查询毫无意义，可能被判成"有遮挡"，
	//   表现是画面出现大片莫名其妙的黑边。
	//   边界色设成白色（深度 1.0 = 最远）表示"什么都没挡"，才是正确语义。
	//
	// 【为什么用 NEAREST】插值出来的深度值没有物理意义
	void SetDepthTextureParameters()
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

		const float borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
	
	}
}

FrameBuffer::FrameBuffer(FrameBufferSpec spec)
	: m_Spec(spec)
{
	if (m_Spec.Width == 0 || m_Spec.Height == 0)
	{
		throw std::runtime_error("[FrameBuffer] spec width/height must not be 0");
	}

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
	if (width ==m_Spec.Width && height == m_Spec.Height)
	{
		return;
	}

	m_Spec.Width = width;
	m_Spec.Height = height;
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

	const GLsizei width = static_cast<GLsizei>(m_Spec.Width);
	const GLsizei height = static_cast<GLsizei>(m_Spec.Height);

	// ---- 颜色附件 ----
	if (!m_Spec.DepthOnly)
	{
		glGenTextures(1, &m_ColorAttachment);
		glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
		if (m_Spec.ColorFormat == TextureFormat::RGBA16F)
		{
			glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F, width, height, 0,
				GL_RGBA, GL_FLOAT, nullptr);

		}
		else if(m_Spec.ColorFormat == TextureFormat::RGBA8)
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		}
		else
		{
			throw std::runtime_error("[FrameBuffer] unknown color format");
		}

		// 【这里千万不能再补一次 glTexImage2D】
		//
		// glTexImage2D 不是"创建纹理"，而是"【分配 / 重定义存储】"——
		// 对同一个纹理 ID 再调一次，会把上一次的分配【整个丢掉重新分配】，
		// 格式也跟着换掉。不报错、不警告，第二次调用直接赢。
		//
		// 这个坑的形态很典型：上面刚写完 if/else 分支，下面原本那行
		// "万能"调用忘了删 —— 于是 RGBA16F 被当场覆盖回 RGBA8。
		// 症状是"HDR 完全没生效 + 调试视图全黑"，
		// 而你会先去怀疑光照强度、怀疑色调映射曲线

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// CLAMP_TO_EDGE：显示尺寸和渲染尺寸不一致时，UV 会在边缘做插值，
		// 用 REPEAT 会在边框上采到对侧的像素（表现为画面边缘有一条杂色）
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);
	}
	

	// ---- 深度附件----
	glGenTextures(1, &m_DepthAttachment);
	glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
	if (m_Spec.DepthOnly || m_Spec.NeedDepth)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
			GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		SetDepthTextureParameters();
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_TEXTURE_2D, m_DepthAttachment, 0);
	}
	else
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height,
					 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
		SetDepthTextureParameters();
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);
	}

	// ---- 只有"完全没有颜色附件"的 FBO 才设 GL_NONE ----
	//
	// 【这里的判据是 DepthOnly，不能带 NeedDepth】
	//
	// glDrawBuffer(GL_NONE) 的语义是"这个 FBO 不往任何颜色附件输出"。
	// 它只对【整个 FBO 都没有颜色附件】的帧缓冲成立 —— 也就是纯深度图。
	// 一旦把"需要深度、但也需要颜色"的 FBO（sceneBuffer、postProcessBuffer、
	// bloom 每一级）也卷进来，后果是：
	//     glClear(GL_COLOR_BUFFER_BIT)  变成空操作
	//     片元着色器的输出               被丢弃
	//   —— 缓冲区从头到尾没被写过，面板全黑
	//
	// 【为什么它极其难查】在 GL 3.3 里它【不是错误】：
	//   GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER 在 GL 3.0 就被删掉了
	//   （3.0 起 GL_NONE 永远是合法的 draw buffer 值），
	//   所以 glCheckFramebufferStatus 返回 COMPLETE、glGetError 返回 0，
	//   连 FBO 完整性检查都拦不住它。
	//   顺带：glReadBuffer(GL_NONE) 会让 glReadPixels 报 1282 ——
	//   之前几轮"回读总是失败"的怪事，根因也在这里
	if (m_Spec.DepthOnly)
	{
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}
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
	if (m_DepthAttachment != 0)
	{
		glDeleteTextures(1, &m_DepthAttachment);
		m_DepthAttachment = 0;
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
