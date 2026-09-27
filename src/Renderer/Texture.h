#pragma once

#include <string>
#include <cstdint>

// 采样参数用枚举而不是 GL 常量，是为了让头文件不必 include <glad/glad.h>。
// 一旦头文件里出现 GL_NEAREST 这类符号，每个包含 Texture.h 的文件
// 都会被动依赖 OpenGL 头，以后换 loader / 换 GL 版本时波及面会很大

enum class TextureFilter
{
	Nearest,   // 取最近的一个纹素：像素风，或必须保持锐利的棋盘格
	Linear,    // 相邻纹素做双线性插值：放大时平滑
};

enum class TextureWrap
{
	Repeat,          // 超出 [0,1] 时平铺，重复纹理（棋盘格、地板）必需
	ClampToEdge,     // 边缘拉伸，适合不该重复的贴图
	MirroredRepeat,  // 镜像平铺
};

struct TextureSpec
{
	TextureFilter MinFilter = TextureFilter::Linear;   // 缩小时（远处）
	TextureFilter MagFilter = TextureFilter::Linear;   // 放大时（近处）
	TextureWrap   WrapS     = TextureWrap::Repeat;
	TextureWrap   WrapT     = TextureWrap::Repeat;

	// mipmap 只对 MinFilter 有意义：它是一串逐级减半的预滤波副本，
	// 缩小采样时取金字塔上层，是消除远距离摩尔纹的标准手段。
	// 放大时用不到（MagFilter 不接受 MIPMAP_* 的取值）
	bool GenerateMipmaps = true;

	// 这张贴图存的是"颜色"还是"数据"？
	//   true —— 颜色贴图（albedo / 漫反射 / 自发光）
	//     内部格式用 GL_SRGB8_ALPHA8，采样时由硬件自动做 sRGB -> 线性 解码。
	//     光照必须在线性空间里计算，所以这一步是必需的，而且零开销
	//   false —— 数据贴图（法线 / 粗糙度 / 金属度 / AO / 高度）
	//     内部格式用 GL_RGBA8 原样保留。它们是物理量，
	//     本来就不是 sRGB 编码的，一旦被"解码"反而会把光照算错
	//
	// 默认 false 是刻意保守的：把数据贴图误当颜色，后果比反过来严重得多，
	// 而且很难一眼看出来
	bool SRGB = false;
};

// ===========================================================================
//  ZHC - Renderer/Texture.h
//
//  一张 2D 纹理。
//
//  职责边界：
//    管：用 stb_image 解码图片 -> 创建 GL 纹理 -> 设置采样参数
//    不管：把纹理喂给 shader。那是调用方的事：Bind(slot) + SetInt("u_Texture", slot)
//
//  坐标系提醒：
//    图片文件的原点在左上，GL 纹理的原点在左下。
//    本类在解码时统一做一次上下翻转，所以拿到纹理后 UV 的 (0,0)
//    对应图片的左下角 —— 和 ImGui::Image 显示 FBO 时用的是同一套约定，
//    以后把两者混用时不会出现一个正一个反
//
//  生命周期：
//    持有 GL 纹理对象，必须在 GL 上下文有效期内构造与析构，
//    也就是在 main 里声明在 Window 之后（这样它先于 Window 析构）
// ===========================================================================
class Texture
{
public:
	// 加载失败抛 std::runtime_error。失败最常见的原因是工作目录不对，
	// 所以消息里会带上 stbi_failure_reason()
	explicit Texture(const std::string& filepath, const TextureSpec& spec = TextureSpec());

	~Texture();

	// 持有 GL 对象，禁止拷贝（拷贝会重复释放同一张纹理）
	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	// 绑定到指定纹理单元：内部先 glActiveTexture(GL_TEXTURE0 + slot)，再 glBindTexture
	void Bind(uint32_t slot = 0) const;
	void Unbind() const;

	uint32_t GetRendererID() const { return m_RendererID; }

	// stb_image 的宽高就是 int，这里保持一致，避免来回转换
	int32_t GetWidth()  const { return m_Width; }
	int32_t GetHeight() const { return m_Height; }

	// 【原图】自带的通道数（1=灰度 2=灰度+alpha 3=RGB 4=RGBA）。
	// 本类统一按 RGBA 上传，所以这个值只用于调试显示
	int32_t GetChannelCount() const { return m_ChannelCount; }

	const std::string& GetPath() const { return m_FilePath; }

private:
	// 成员声明顺序与构造函数的初始化列表保持一致
	std::string m_FilePath;
	uint32_t    m_RendererID = 0;
	int32_t     m_Width = 0;
	int32_t     m_Height = 0;
	int32_t     m_ChannelCount = 0;
};
