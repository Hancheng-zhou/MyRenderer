#pragma once
#include <string>
#include <memory>
#include <vector>
#include "Renderer/Texture.h"
struct TextureEntry
{
	std::string Name;
	std::string Path;
	std::shared_ptr<Texture> Handle;
};
std::shared_ptr<Texture> TryLoadTexture(const std::string& filepath,
	const TextureSpec& spec,
	std::string* outError);
std::vector<TextureEntry> LoadTexturesFromDirectory(const std::string& directory,
	const TextureSpec& spec);