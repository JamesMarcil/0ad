/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INCLUDED_RENDERER_BACKEND_GL_SWAPCHAIN
#define INCLUDED_RENDERER_BACKEND_GL_SWAPCHAIN

#include "lib/config2.h"
#include "renderer/backend/gl/Framebuffer.h"
#include "renderer/backend/ISwapChain.h"

#if CONFIG2_GLES
#include "external_libraries/opengles2_wrapper.h"
#else
#include <glad/gl.h>
#endif

#include <cstdint>
#include <memory>
#include <unordered_map>

typedef struct SDL_Window SDL_Window;

namespace Renderer
{

namespace Backend
{

namespace GL
{

class CDevice;

class CSwapChain final : public ISwapChain
{
public:
	~CSwapChain() override;

	IDevice* GetDevice() override;

	bool IsValid() const override { return true; }

	bool AcquireNextBackbuffer() override;

	IFramebuffer* GetCurrentBackbuffer(
		const AttachmentLoadOp colorAttachmentLoadOp,
		const AttachmentStoreOp colorAttachmentStoreOp,
		const AttachmentLoadOp depthStencilAttachmentLoadOp,
		const AttachmentStoreOp depthStencilAttachmentStoreOp) override;

	void Present() override;

private:
	friend class CDevice;

	static std::unique_ptr<CSwapChain> Create(
		CDevice* device, SDL_Window* window,
		int surfaceDrawableWidth, int surfaceDrawableHeight,
		const bool vsync);

	CSwapChain();

	CDevice* m_Device{nullptr};
	SDL_Window* m_Window{nullptr};
	int m_SurfaceDrawableWidth{0};
	int m_SurfaceDrawableHeight{0};

	using BackbufferKey = std::tuple<
		AttachmentLoadOp, AttachmentStoreOp,
		AttachmentLoadOp, AttachmentStoreOp>;
	struct BackbufferKeyHash
	{
		size_t operator()(const BackbufferKey& key) const;
	};
	// We use std::unordered_map to avoid storing sizes of Attachment*Op
	// enumerations. If it becomes a performance issue we'll replace it
	// by an array.
	std::unordered_map<
		BackbufferKey, std::unique_ptr<CFramebuffer>, BackbufferKeyHash> m_Backbuffers;
	bool m_BackbufferAcquired{false};
};

} // namespace GL

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_GL_SWAPCHAIN
