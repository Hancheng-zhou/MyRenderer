#include "Texture.h"

#include <glad/glad.h>
#include <stdexcept>
#include <string>

// stb_image 的实现已经编译在 stb_image.cpp 里，这里只包含声明
#include <stb_image.h>

namespace
{
	GLenum ToGL(TextureFilter filter)
	{
		switch (filter)
		{
		case TextureFilter::Nearest: return GL_NEAREST;
		case TextureFilter::Linear:  return GL_LINEAR;
		}
		return GL_LINEAR;
	}

	GLenum ToGL(TextureWrap wrap)
	{
		switch (wrap)
		{
		case TextureWrap::Repeat:         return GL_REPEAT;
		case TextureWrap::ClampToEdge:    return GL_CLAMP_TO_EDGE;
		case TextureWrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
		}
		return GL_REPEAT;
	}
}

Texture::Texture(const std::string& filepath, const TextureSpec& spec)
	: m_FilePath(filepath)
{
	// 1) 上下翻转。必须放在 stbi_load 之前：它改变的是解码时写入内存的行序，
	//    放在之后调是没有用的。
	//    注意这是 stb_image 的【全局开关】，会影响之后所有的加载
	stbi_set_flip_vertically_on_load(1);

	// 2) 最后一个参数 4 = 不论原图几通道，一律转成 RGBA。
	//    这样上传时的 format 永远可以是 GL_RGBA，
	//    也顺带绕开了 RGB（3 字节/像素）在宽度不是 4 的倍数时需要的
	//    glPixelStorei(GL_UNPACK_ALIGNMENT, 1) —— 行对齐这个坑直接消失
	//
	//    注意：m_ChannelCount 拿到的仍然是【原图】的通道数，这是 stb 的约定
	stbi_uc* data = stbi_load(filepath.c_str(), &m_Width, &m_Height, &m_ChannelCount, 4);

	// 3) 必须检查失败：nullptr 直接往下走，会静默产生一张黑纹理，
	//    排查的时候根本想不到是加载失败
	if (data == nullptr)
	{
		throw std::runtime_error("Failed to load texture: " + filepath +
		                         " (" + stbi_failure_reason() + ")");
	}

	glGenTextures(1, &m_RendererID);
	glBindTexture(GL_TEXTURE_2D, m_RendererID);

	// 4) 上传像素。
	//    type 必须和数据的实际类型匹配：stbi_uc 是每分量 1 字节 -> GL_UNSIGNED_BYTE。
	//    写成 GL_UNSIGNED_INT 的话，GL 会按每像素 16 字节去读这块内存，
	//    读取量变成实际的 4 倍，直接越界
	//
	//    内部格式还决定"采样时要不要做色彩空间转换"：
	//    SRGB8_ALPHA8 会让硬件自动把 sRGB 解码成线性，代价为零
	//    （存储仍是每分量 8 位，只是多带了一个"这是 sRGB"的标记）。
	//
	//    这里不能写死：颜色贴图需要解码，而法线 / 粗糙度这类数据贴图
	//    是物理量、本来就是线性的，被解码一次反而会把光照算错
	const GLint internalFormat = spec.SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height, 0,
	             GL_RGBA, GL_UNSIGNED_BYTE, data);

	// 5) 生成 mipmap：必须在 glTexImage2D 之后。
	//    如果 MinFilter 用了 MIPMAP_* 却没生成过 mipmap，纹理会被判为"不完整"，
	//    采样结果直接是黑色，而且 glGetError 依然返回 GL_NO_ERROR（静默失败）
	if (spec.GenerateMipmaps)
	{
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	// 6) 采样参数。Wrap 的 S / T 必须都设，只设一个是最常见的疏漏
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGL(spec.WrapS));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGL(spec.WrapT));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ToGL(spec.MagFilter));

	// MinFilter 要结合"有没有 mipmap"来选。GL 一共接受 6 种取值，
	// 这里只挑两种最常用的：不用 mipmap 时是纯 NEAREST / LINEAR，
	// 用 mipmap 时在两级金字塔之间做线性插值（最平滑的一档）
	const GLenum minFilter = spec.GenerateMipmaps
		? (spec.MinFilter == TextureFilter::Nearest ? GL_NEAREST_MIPMAP_LINEAR
		                                            : GL_LINEAR_MIPMAP_LINEAR)
		: (spec.MinFilter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);

	// 7) 像素已经拷进显存，CPU 侧这份要还给 stb。
	//    它用的是 STBI_MALLOC（默认是 malloc，但可以配置），
	//    所以必须用配对的 stbi_image_free，不能用 free / delete。
	//    漏掉这一步就是每张纹理泄漏一份内存
	stbi_image_free(data);

	glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::~Texture()
{
	// 必须在 GL 上下文有效期内执行，所以 Texture 要声明在 Window 之后
	if (m_RendererID != 0)
	{
		glDeleteTextures(1, &m_RendererID);
		m_RendererID = 0;
	}
}

void Texture::Bind(uint32_t slot) const
{
	// 顺序不能反：先切"活跃的纹理单元"，再绑纹理。
	// 反过来的话纹理会被绑到【上一个】活跃单元上，
	// 表现是采样到别的纹理或者一片黑，而且不报任何错
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, m_RendererID);
}

void Texture::Unbind() const
{
	glBindTexture(GL_TEXTURE_2D, 0);
}
