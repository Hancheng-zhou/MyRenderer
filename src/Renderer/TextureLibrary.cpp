#include "TextureLibrary.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>
namespace
{
	// stb_image 支持的格式。扩展名只是初筛，省掉无谓的解码尝试；
	// 真正能不能读出来交给 TryLoadTexture 判断
	const char* const k_SupportedExtensions[] = {
		".png", ".jpg", ".jpeg", ".bmp", ".tga", ".psd", ".gif", ".hdr", ".pic", ".ppm", ".pgm"
	};

	bool HasSupportedExtension(const std::filesystem::path& path)
	{
		// extension() 保留原大小写（Photo.JPG 和 photo.jpg 都可能出现），
		// 所以必须转小写再比。传 unsigned char 是为了避开
		// std::tolower 对负值实参的未定义行为
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		for (const char* supported : k_SupportedExtensions)
		{
			if (ext == supported)
			{
				return true;
			}
		}
		return false;
	}
}

std::shared_ptr<Texture> TryLoadTexture(const std::string& filepath,
	const TextureSpec& spec,
	std::string* outError)
{
	try
	{
		return std::make_shared<Texture>(filepath, spec);
	}
	catch (const std::exception& e)
	{
		if (outError)
		{
			// 原样带出 stbi_failure_reason() 之类的原因，
			// 不要替换成"加载失败"这种查不出问题的笼统说法
			*outError = e.what();
		}
		return nullptr;
	}
}

std::vector<TextureEntry> LoadTexturesFromDirectory(const std::string& directory,
	const TextureSpec& spec)
{
	std::vector<TextureEntry> entries;

	// 【必须用带 error_code 的重载】不带 error_code 的那版在目录不存在时
	// 会【抛异常】—— 启动时把 textures 目录名打错一个字母，程序就直接崩
	std::error_code ec;
	std::filesystem::directory_iterator it(directory, ec);
	if (ec)
	{
		std::fprintf(stderr, "[TextureLibrary] 无法打开目录 '%s': %s\n",
			directory.c_str(), ec.message().c_str());
		return entries;
	}

	// 先把路径收集起来再统一排序。
	//
	// 【为什么必须排序】directory_iterator 的遍历顺序是【未定义的】，
	// 由文件系统决定。不排的话每次运行下拉框里的顺序都不一样，
	// 想复现上次的观察条件就得靠运气 —— 这跟你做 4 个视角预设的动机是一样的
	std::vector<std::filesystem::path> files;
	for (const auto& dirEntry : it)
	{
		const bool isFile = dirEntry.is_regular_file(ec);
		if (ec || !isFile)
		{
			continue;
		}
		if (HasSupportedExtension(dirEntry.path()))
		{
			files.push_back(dirEntry.path());
		}
	}

	std::sort(files.begin(), files.end(),
		[](const std::filesystem::path& a, const std::filesystem::path& b)
		{
			return a.filename().string() < b.filename().string();
		});

	entries.reserve(files.size());

	for (const std::filesystem::path& file : files)
	{
		const std::string path = file.string();

		std::string error;
		if (auto handle = TryLoadTexture(path, spec, &error))
		{
			// 显示名用去掉扩展名的文件名，比整条路径短得多
			entries.push_back({ file.stem().string(), path, handle });
		}
		else
		{
			std::fprintf(stderr, "[TextureLibrary] 跳过 '%s': %s\n", path.c_str(), error.c_str());
		}
	}

	std::printf("[TextureLibrary] '%s'：载入 %zu 张\n", directory.c_str(), entries.size());

	return entries;
}

