#include "RenderCommand.h"
#include <glad/glad.h>
#include <Renderer/VertexArray.h>
void RenderCommand::Init()
{
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glDepthFunc(GL_LESS);
}

void RenderCommand::SetViewport(int32_t x, int32_t y, int32_t width, int32_t height)
{
	glViewport(static_cast<GLint>(x), static_cast<GLint>(y),
		static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

//深度测试管要不要深度比较
//深度写入管要不要写入深度缓冲
void RenderCommand::SetDepthTest(bool enable)
{
	if(enable)
		glEnable(GL_DEPTH_TEST);
	else
	glDisable(GL_DEPTH_TEST);
}

void RenderCommand::SetDepthWrite(bool enable)
{
	glDepthMask(enable ? GL_TRUE : GL_FALSE);
}

void RenderCommand::SetClearColor(const glm::vec4& color)
{
	glClearColor(color.r, color.g, color.b, color.a);
}

void RenderCommand::Clear()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RenderCommand::ClearDepth()
{
	glClear(GL_DEPTH_BUFFER_BIT);
}

void RenderCommand::SetCullMode(bool cullFront)
{
	glCullFace(cullFront ? GL_FRONT : GL_BACK);
}

void RenderCommand::BindTexture(uint32_t rendererID, uint32_t slot)
{
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, rendererID);
}

void RenderCommand::DrawArrays(const VertexArray& vertexArray, uint32_t vertexCount)
{
	glBindVertexArray(vertexArray.GetRendererID());
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount));
}

void RenderCommand::DrawIndexed(const VertexArray& vertexArray, uint32_t indexCount)
{
	glBindVertexArray(vertexArray.GetRendererID());
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
}
