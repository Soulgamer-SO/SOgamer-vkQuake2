/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2018-2019 Krzysztof Kondrak

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/*
** VK_COMMON.C
**
** This file implements the operating system binding of Vk to QVk function
** pointers.  When doing a port of Quake2 you must implement the following
** two functions:
**
** QVk_Init() - loads libraries, assigns function pointers, etc.
** QVk_Shutdown() - unloads libraries, NULLs function pointers
*/
#include <float.h>
#include "../ref_vk/vk_local.h"
#ifdef _WIN32
#include "../win32/vk_win.h"
#endif
#ifdef __linux__
#include "../linux/vk_linux.h"
#endif
#ifdef __APPLE__
#include "../macos/vk_macos.h"
#endif

FILE *vk_logfp = NULL;

// Vulkan instance, surface and memory allocator
VkInstance	 vk_instance = VK_NULL_HANDLE;
VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
VmaAllocator vk_malloc  = VK_NULL_HANDLE;

// Vulkan device
qvkdevice_t	 vk_device = {
	.physical = VK_NULL_HANDLE,
	.logical = VK_NULL_HANDLE,
	.gfxQueue = VK_NULL_HANDLE,
	.presentQueue = VK_NULL_HANDLE,
	.transferQueue = VK_NULL_HANDLE,
	.gfxFamilyIndex = -1,
	.presentFamilyIndex = -1,
	.transferFamilyIndex = -1
};

// Vulkan swapchain
qvkswapchain_t vk_swapchain = {
	.sc = VK_NULL_HANDLE,
	.format = VK_FORMAT_UNDEFINED,
	.presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
	.extent = { 0, 0 },
	.images = NULL,
	.imageCount = 0
};

// Vulkan renderpasses
qvkrenderpass_t vk_renderpasses[RP_COUNT] = { 
	{
		.rp = VK_NULL_HANDLE,
		.colorLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.sampleCount = VK_SAMPLE_COUNT_1_BIT
	},
	{
		.rp = VK_NULL_HANDLE,
		.colorLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.sampleCount = VK_SAMPLE_COUNT_1_BIT
	},
	{
		.rp = VK_NULL_HANDLE,
		.colorLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.sampleCount = VK_SAMPLE_COUNT_1_BIT
	}
};

// Vulkan pools
VkCommandPool vk_commandPool = VK_NULL_HANDLE;
VkCommandPool vk_transferCommandPool = VK_NULL_HANDLE;
VkDescriptorPool vk_descriptorPool = VK_NULL_HANDLE;
static VkCommandPool vk_stagingCommandPool = VK_NULL_HANDLE;
// Vulkan image views
VkImageView *vk_imageviews = NULL;
// Vulkan framebuffers
VkFramebuffer *vk_framebuffers[RP_COUNT];
// depth buffer
qvktexture_t vk_colorbuffer = QVVKTEXTURE_INIT;
qvktexture_t vk_colorbufferWarp = QVVKTEXTURE_INIT;
qvktexture_t vk_depthbuffer = QVVKTEXTURE_INIT;
qvktexture_t vk_ui_depthbuffer = QVVKTEXTURE_INIT;
// render targets for MSAA
qvktexture_t vk_msaaColorbuffer = QVVKTEXTURE_INIT;
// viewport and scissor
VkViewport vk_viewport = { .0f, .0f, .0f, .0f, .0f, .0f };
VkRect2D vk_scissor = { { 0, 0 }, { 0, 0 } };

#define NUM_CMDBUFFERS 2
// Vulkan command buffers
VkCommandBuffer *vk_commandbuffers = NULL;
// command buffer double buffering fences
VkFence vk_fences[NUM_CMDBUFFERS];
// semaphore: signal when next image is available for rendering
VkSemaphore vk_imageAvailableSemaphores[NUM_CMDBUFFERS];
// semaphore: signal when rendering to current command buffer is complete
VkSemaphore vk_renderFinishedSemaphores[NUM_CMDBUFFERS];
// tracker variables
VkCommandBuffer vk_activeCmdbuffer = VK_NULL_HANDLE;
// index of active command buffer
int vk_activeBufferIdx = 0;
// index of currently acquired image
uint32_t vk_imageIndex = 0;
// index of currently used staging buffer
int vk_activeStagingBuffer = 0;
// started rendering frame?
static qboolean vk_frameStarted = false;

// render pipelines
qvkpipeline_t vk_drawTexQuadPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawColorQuadPipeline[2]  = { QVKPIPELINE_INIT, QVKPIPELINE_INIT };
qvkpipeline_t vk_drawModelPipelineStrip[2] = { QVKPIPELINE_INIT, QVKPIPELINE_INIT };
qvkpipeline_t vk_drawModelPipelineFan[2]   = { QVKPIPELINE_INIT, QVKPIPELINE_INIT };
qvkpipeline_t vk_drawNoDepthModelPipelineStrip = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawNoDepthModelPipelineFan = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawLefthandModelPipelineStrip = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawLefthandModelPipelineFan = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawNullModel = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawParticlesPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawPointParticlesPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawSpritePipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawPolyPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawPolyLmapPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawPolyWarpPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawBeamPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawSkyboxPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_drawDLightPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_showTrisPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_shadowsPipelineStrip = QVKPIPELINE_INIT;
qvkpipeline_t vk_shadowsPipelineFan = QVKPIPELINE_INIT;
qvkpipeline_t vk_postprocessPipeline = QVKPIPELINE_INIT;
qvkpipeline_t vk_worldWarpPipeline = QVKPIPELINE_INIT;

// samplers
static VkSampler vk_samplers[S_SAMPLER_CNT];

PFN_vkCreateDebugUtilsMessengerEXT qvkCreateDebugUtilsMessengerEXT;
PFN_vkDestroyDebugUtilsMessengerEXT qvkDestroyDebugUtilsMessengerEXT;

#define VK_INPUTBIND_DESC(s) { \
	.binding = 0, \
	.stride = s, \
	.inputRate = VK_VERTEX_INPUT_RATE_VERTEX \
};

#define VK_INPUTATTR_DESC(l, f, o) { \
	.binding = 0, \
	.location = l, \
	.format = f, \
	.offset = o \
}

#define VK_VERTEXINPUT_CINF(b, a) { \
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, \
	.pNext = NULL, \
	.flags = 0, \
	.vertexBindingDescriptionCount = 1, \
	.pVertexBindingDescriptions = &b, \
	.vertexAttributeDescriptionCount = sizeof(a) / sizeof(a[0]), \
	.pVertexAttributeDescriptions = a \
}

#define VK_VERTINFO(name, bindSize, ...) \
	VkVertexInputAttributeDescription attrDesc##name[] = { __VA_ARGS__ }; \
	VkVertexInputBindingDescription name##bindingDesc = VK_INPUTBIND_DESC(bindSize); \
	VkPipelineVertexInputStateCreateInfo vertInfo##name = VK_VERTEXINPUT_CINF(name##bindingDesc, attrDesc##name);

#define VK_LOAD_VERTFRAG_SHADERS(shaders, namevert, namefrag) \
	vkDestroyShaderModule(vk_device.logical, shaders[0].module, NULL); \
	vkDestroyShaderModule(vk_device.logical, shaders[1].module, NULL); \
	shaders[0] = QVk_CreateShader(namevert##_vert_spv, namevert##_vert_size, VK_SHADER_STAGE_VERTEX_BIT); \
	shaders[1] = QVk_CreateShader(namefrag##_frag_spv, namefrag##_frag_size, VK_SHADER_STAGE_FRAGMENT_BIT);

// global static buffers (reused, never changing)
qvkbuffer_t vk_texRectVbo;
qvkbuffer_t vk_colorRectVbo;
qvkbuffer_t vk_rectIbo;

// global dynamic buffers (double buffered)
#define NUM_DYNBUFFERS 2
static qvkbuffer_t vk_dynVertexBuffers[NUM_DYNBUFFERS];
static qvkbuffer_t vk_dynIndexBuffers[NUM_DYNBUFFERS];
static qvkbuffer_t vk_dynUniformBuffers[NUM_DYNBUFFERS];
static VkDescriptorSet vk_uboDescriptorSets[NUM_DYNBUFFERS];
static qvkstagingbuffer_t vk_stagingBuffers[NUM_DYNBUFFERS];
static int vk_activeDynBufferIdx = 0;
static int vk_activeSwapBufferIdx = 0;

// index buffer for triangle fan emulation - all because Metal/MoltenVK don't support them
static VkBuffer *vk_triangleFanIbo = NULL;
static uint32_t  vk_triangleFanIboUsage = 0;

// swap buffers used if primary dynamic buffers get full
#define NUM_SWAPBUFFER_SLOTS 4
static int vk_swapBuffersCnt[NUM_SWAPBUFFER_SLOTS];
static int vk_swapDescSetsCnt[NUM_SWAPBUFFER_SLOTS];
static qvkbuffer_t *vk_swapBuffers[NUM_SWAPBUFFER_SLOTS];
static VkDescriptorSet *vk_swapDescriptorSets[NUM_SWAPBUFFER_SLOTS];
VkDescriptorSet vk_iaDescriptorSet;

// by how much will the dynamic buffers be resized if we run out of space?
#define BUFFER_RESIZE_FACTOR 2.f
// size in bytes used for uniform descriptor update
#define UNIFORM_ALLOC_SIZE 1024
// start values for dynamic buffer sizes - bound to change if the application runs out of space (sizes in bytes)
#define VERTEX_BUFFER_SIZE (1024 * 1024)
#define INDEX_BUFFER_SIZE (2 * 1024)
#define UNIFORM_BUFFER_SIZE (2048 * 1024)
// staging buffer is constant in size but has a max limit beyond which it will be submitted
#define STAGING_BUFFER_MAXSIZE (8192 * 1024)
// initial index count in triangle fan buffer - assuming 200 indices (200*3 = 600 triangles) per object
#define TRIANGLE_FAN_INDEX_CNT 200

// Vulkan common descriptor sets for UBO, primary texture sampler and optional lightmap texture
VkDescriptorSetLayout vk_uboDescSetLayout;
VkDescriptorSetLayout vk_samplerDescSetLayout;
VkDescriptorSetLayout vk_samplerLightmapDescSetLayout;
// descriptor set layout for postprocessing input attachment
VkDescriptorSetLayout vk_iaDescSetLayout;

extern cvar_t *vk_msaa;

VkFormat QVk_FindDepthFormat()
{
	VkFormat depthFormats[] = {
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D16_UNORM_S8_UINT,
		VK_FORMAT_D16_UNORM
	};

	for (int i = 0; i < 5; ++i)
	{
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(vk_device.physical, depthFormats[i], &formatProps);

		if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			return depthFormats[i];
	}

	return VK_FORMAT_D16_UNORM;
}

// internal helper
static VkSampleCountFlagBits GetSampleCount()
{
	static VkSampleCountFlagBits msaaModes[] = {
		VK_SAMPLE_COUNT_1_BIT,
		VK_SAMPLE_COUNT_2_BIT,
		VK_SAMPLE_COUNT_4_BIT,
		VK_SAMPLE_COUNT_8_BIT,
		VK_SAMPLE_COUNT_16_BIT
	};

	return msaaModes[(int)vk_msaa->value];
}

// internal helper
static void DestroyImageViews()
{
	if(!vk_imageviews)
		return;

	for (int i = 0; i < vk_swapchain.imageCount; i++)
	{
		vkDestroyImageView(vk_device.logical, vk_imageviews[i], NULL);
	}
	free(vk_imageviews);
	vk_imageviews = NULL;
}

// internal helper
static VkResult CreateImageViews()
{
	VkResult res = VK_SUCCESS;
	vk_imageviews = (VkImageView *)malloc(vk_swapchain.imageCount * sizeof(VkImageView));

	for (size_t i = 0; i < vk_swapchain.imageCount; ++i)
	{
		VkResult res = QVk_CreateImageView(&vk_swapchain.images[i], VK_IMAGE_ASPECT_COLOR_BIT, &vk_imageviews[i], vk_swapchain.format, 1);

		if (res != VK_SUCCESS)
		{
			DestroyImageViews();
			return res;
		}
	}

	return res;
}

// internal helper
static void DestroyFramebuffers()
{
	for (int f = 0; f < RP_COUNT; f++)
	{
		if (vk_framebuffers[f])
		{
			for (int i = 0; i < vk_swapchain.imageCount; ++i)
			{
				vkDestroyFramebuffer(vk_device.logical, vk_framebuffers[f][i], NULL);
			}

			free(vk_framebuffers[f]);
			vk_framebuffers[f] = NULL;
		}
	}
}

// internal helper
static VkResult CreateFramebuffers()
{
	VkResult res = VK_SUCCESS;
	vk_framebuffers[RP_WORLD] = (VkFramebuffer *)malloc(vk_swapchain.imageCount * sizeof(VkFramebuffer));
	vk_framebuffers[RP_WORLD_WARP] = (VkFramebuffer *)malloc(vk_swapchain.imageCount * sizeof(VkFramebuffer));
	vk_framebuffers[RP_UI] = (VkFramebuffer *)malloc(vk_swapchain.imageCount * sizeof(VkFramebuffer));

	VkFramebufferCreateInfo fbCreateInfos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.renderPass = vk_renderpasses[RP_WORLD].rp,
			.attachmentCount = (vk_renderpasses[RP_WORLD].sampleCount != VK_SAMPLE_COUNT_1_BIT) ? 3 : 2,
			.width = vk_swapchain.extent.width,
			.height = vk_swapchain.extent.height,
			.layers = 1
		},
		{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.renderPass = vk_renderpasses[RP_UI].rp,
			.attachmentCount = 3,
			.width = vk_swapchain.extent.width,
			.height = vk_swapchain.extent.height,
			.layers = 1
		},
		{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.renderPass = vk_renderpasses[RP_WORLD_WARP].rp,
			.attachmentCount = 2,
			.width = vk_swapchain.extent.width,
			.height = vk_swapchain.extent.height,
			.layers = 1
		}
	};

	VkImageView attachments[] = { vk_colorbuffer.imageView, vk_depthbuffer.imageView, vk_msaaColorbuffer.imageView };
	VkImageView attachmentsWarp[] = { vk_colorbuffer.imageView, vk_colorbufferWarp.imageView };

	fbCreateInfos[RP_WORLD].pAttachments = attachments;
	fbCreateInfos[RP_WORLD_WARP].pAttachments = attachmentsWarp;

	for (size_t i = 0; i < vk_swapchain.imageCount; ++i)
	{
		VkImageView attachments[] = { vk_colorbufferWarp.imageView, vk_ui_depthbuffer.imageView, vk_imageviews[i] };

		fbCreateInfos[RP_UI].pAttachments = attachments;
		VkResult result = vkCreateFramebuffer(vk_device.logical, &fbCreateInfos[RP_UI], NULL, &vk_framebuffers[RP_UI][i]);
		result = vkCreateFramebuffer(vk_device.logical, &fbCreateInfos[RP_WORLD], NULL, &vk_framebuffers[RP_WORLD][i]);
		result = vkCreateFramebuffer(vk_device.logical, &fbCreateInfos[RP_WORLD_WARP], NULL, &vk_framebuffers[RP_WORLD_WARP][i]);

		if (result != VK_SUCCESS)
		{
			DestroyFramebuffers();
			return res;
		}
	}

	return res;
}

// internal helper
static VkResult CreateRenderpasses()
{
	qboolean msaaEnabled = vk_renderpasses[RP_WORLD].sampleCount != VK_SAMPLE_COUNT_1_BIT;

	VkSubpassDependency dependencies[2] = {
		{
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
		},
		{
		.srcSubpass = 0,
		.dstSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
		}
	};

	VkAttachmentDescription attachments[] = {
		// color attachment
		{
			.flags = 0,
			.format = vk_swapchain.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = msaaEnabled ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : vk_renderpasses[RP_WORLD].colorLoadOp,
			// if MSAA is enabled, we don't need to preserve rendered texture data since it's kept by MSAA resolve attachment
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			// treat this attachment as an interim color stage if MSAA is enabled
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		},
		// depth attachment
		{
			.flags = 0,
			.format = QVk_FindDepthFormat(),
			.samples = vk_renderpasses[RP_WORLD].sampleCount,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		},
		// MSAA resolve attachment
		{
			.flags = 0,
			.format = vk_swapchain.format,
			.samples = vk_renderpasses[RP_WORLD].sampleCount,
			.loadOp = msaaEnabled ? vk_renderpasses[RP_WORLD].colorLoadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		}
	};

	VkAttachmentReference colorAttachmentRef = {
		.attachment = msaaEnabled ? 2 : 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkAttachmentReference depthAttachmentRef = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	VkAttachmentReference colorAttachmentResolveMSAARef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkSubpassDescription subpassDesc = {
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = NULL,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentRef,
		.pResolveAttachments = msaaEnabled ? &colorAttachmentResolveMSAARef : NULL,
		.pDepthStencilAttachment = &depthAttachmentRef,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = NULL
	};

	VkRenderPassCreateInfo rpCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.attachmentCount = msaaEnabled ? 3 : 2,
		.pAttachments = attachments,
		.subpassCount = 1,
		.pSubpasses = &subpassDesc
	};

	rpCreateInfo.dependencyCount = 2;
	rpCreateInfo.pDependencies = dependencies;

	vkCreateRenderPass(vk_device.logical, &rpCreateInfo, NULL, &vk_renderpasses[RP_WORLD].rp);

	// World warp
	VkAttachmentDescription warpAttachments[] = {
		// color attachment
		{
			.flags = 0,
			.format = vk_swapchain.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
			// if MSAA is enabled, we don't need to preserve rendered texture data since it's kept by MSAA resolve attachment
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			// treat this attachment as an interim color stage if MSAA is enabled
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		},
		// color attachment out
		{
			.flags = 0,
			.format = vk_swapchain.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, // : vk_renderpasses[RP_WORLD].colorLoadOp,
			// if MSAA is enabled, we don't need to preserve rendered texture data since it's kept by MSAA resolve attachment
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			// treat this attachment as an interim color stage if MSAA is enabled
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		}
	};

	VkAttachmentReference colorAttachmentIn = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkAttachmentReference colorAttachmentOut = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkSubpassDescription warpSubpassDescs[] = {
		{
			.flags = 0,
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 1,
			.pInputAttachments = &colorAttachmentIn,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentOut,
			.pResolveAttachments = NULL,
			.pDepthStencilAttachment = NULL,
			.preserveAttachmentCount = 0,
			.pPreserveAttachments = NULL
		}
	};

	VkRenderPassCreateInfo warpRpCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.attachmentCount = 2,
		.pAttachments = warpAttachments,
		.subpassCount = 1,
		.pSubpasses = warpSubpassDescs
	};

	warpRpCreateInfo.dependencyCount = 2;
	warpRpCreateInfo.pDependencies = dependencies;

	vkCreateRenderPass(vk_device.logical, &warpRpCreateInfo, NULL, &vk_renderpasses[RP_WORLD_WARP].rp);

	// UI
	VkAttachmentDescription uiAttachments[] = {
		// color attachment
		{
			.flags = 0,
			.format = vk_swapchain.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
			// if MSAA is enabled, we don't need to preserve rendered texture data since it's kept by MSAA resolve attachment
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			// treat this attachment as an interim color stage if MSAA is enabled
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		},
		// depth attachment - because of player model preview in settings screen
		{
			.flags = 0,
			.format = QVk_FindDepthFormat(),
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		},
		// MSAA resolve attachment
		{
			.flags = 0,
			.format = vk_swapchain.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		}
	};

	VkAttachmentReference color_input_attachment_reference;
	color_input_attachment_reference.attachment = 0;
	color_input_attachment_reference.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference ui_color_attachment_reference;
	ui_color_attachment_reference.attachment = 0;
	ui_color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference uiDepthAttachmentRef = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	VkAttachmentReference swap_chain_attachment_reference;
	swap_chain_attachment_reference.attachment = 2;
	swap_chain_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription uiSubpassDescs[] = {
		{
			.flags = 0,
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = NULL,
			.colorAttachmentCount = 1,
			.pColorAttachments = &ui_color_attachment_reference,
			.pResolveAttachments = NULL,
			.pDepthStencilAttachment = &uiDepthAttachmentRef,
			.preserveAttachmentCount = 0,
			.pPreserveAttachments = NULL
		},
		{
			.flags = 0,
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 1,
			.pInputAttachments = &color_input_attachment_reference,
			.colorAttachmentCount = 1,
			.pColorAttachments = &swap_chain_attachment_reference,
			.pResolveAttachments = NULL,
			.pDepthStencilAttachment = NULL,
			.preserveAttachmentCount = 0,
			.pPreserveAttachments = NULL
		}
	};

	// subpass depencency: wait for color stage
	VkSubpassDependency spDep = {
		.srcSubpass = 0,
		.dstSubpass = 1,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
		.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
	};

	VkRenderPassCreateInfo uiRpCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.attachmentCount = 3,
		.pAttachments = uiAttachments,
		.subpassCount = 2,
		.pSubpasses = uiSubpassDescs
	};

	uiRpCreateInfo.dependencyCount = 1;
	uiRpCreateInfo.pDependencies = &spDep;

	return vkCreateRenderPass(vk_device.logical, &uiRpCreateInfo, NULL, &vk_renderpasses[RP_UI].rp);
}

// internal helper
static void CreateDrawBuffers()
{
	QVk_CreateDepthBuffer(vk_renderpasses[RP_WORLD].sampleCount, &vk_depthbuffer);
	QVk_CreateDepthBuffer(VK_SAMPLE_COUNT_1_BIT, &vk_ui_depthbuffer);
	ri.Con_Printf(PRINT_ALL, "...created depth buffer\n");
	QVk_CreateColorBuffer(VK_SAMPLE_COUNT_1_BIT, &vk_colorbuffer, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	QVk_CreateColorBuffer(VK_SAMPLE_COUNT_1_BIT, &vk_colorbufferWarp, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	QVk_CreateColorBuffer(vk_renderpasses[RP_WORLD].sampleCount, &vk_msaaColorbuffer, 0);
	ri.Con_Printf(PRINT_ALL, "...created MSAAx%d color buffer\n", vk_renderpasses[RP_WORLD].sampleCount);
}

// internal helper
static void DestroyDrawBuffer(qvktexture_t *drawBuffer)
{
	if (drawBuffer->image != VK_NULL_HANDLE)
	{
		vmaDestroyImage(vk_malloc, drawBuffer->image, drawBuffer->allocation);
		vkDestroyImageView(vk_device.logical, drawBuffer->imageView, NULL);
		drawBuffer->image = VK_NULL_HANDLE;
		drawBuffer->imageView = VK_NULL_HANDLE;
	}
}

// internal helper
static void DestroyDrawBuffers()
{
	DestroyDrawBuffer(&vk_depthbuffer);
	DestroyDrawBuffer(&vk_ui_depthbuffer);
	DestroyDrawBuffer(&vk_colorbuffer);
	DestroyDrawBuffer(&vk_colorbufferWarp);
	DestroyDrawBuffer(&vk_msaaColorbuffer);
}

// internal helper
static void CreateDescriptorSetLayouts()
{
	VkDescriptorSetLayoutBinding layoutBinding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.pImmutableSamplers = NULL
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.bindingCount = 1,
		.pBindings = &layoutBinding
	};

	// uniform buffer object layout
	VK_VERIFY(vkCreateDescriptorSetLayout(vk_device.logical, &layoutInfo, NULL, &vk_uboDescSetLayout));

	// sampler layout
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VK_VERIFY(vkCreateDescriptorSetLayout(vk_device.logical, &layoutInfo, NULL, &vk_samplerDescSetLayout));
	// secondary sampler: lightmaps
	VK_VERIFY(vkCreateDescriptorSetLayout(vk_device.logical, &layoutInfo, NULL, &vk_samplerLightmapDescSetLayout));
	// postprocessing input attachment
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	VK_VERIFY(vkCreateDescriptorSetLayout(vk_device.logical, &layoutInfo, NULL, &vk_iaDescSetLayout));
}

// internal helper
static void CreateSamplers()
{
	VkSamplerCreateInfo samplerInfo = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.mipLodBias = 0.f,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1.f,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.f,
		.maxLod = 1.f,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE
	};

	VK_VERIFY(vkCreateSampler(vk_device.logical, &samplerInfo, NULL, &vk_samplers[S_NEAREST]));

	samplerInfo.maxLod = FLT_MAX;
	VK_VERIFY(vkCreateSampler(vk_device.logical, &samplerInfo, NULL, &vk_samplers[S_MIPMAP_NEAREST]));

	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	VK_VERIFY(vkCreateSampler(vk_device.logical, &samplerInfo, NULL, &vk_samplers[S_MIPMAP_LINEAR]));

	samplerInfo.maxLod = 1.f;
	VK_VERIFY(vkCreateSampler(vk_device.logical, &samplerInfo, NULL, &vk_samplers[S_LINEAR]));

	// aniso samplers
	assert((vk_device.properties.limits.maxSamplerAnisotropy > 1.f) && "maxSamplerAnisotropy is 1");

	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = vk_device.properties.limits.maxSamplerAnisotropy;
	samplerInfo.maxLod = 1.f;

	VK_VERIFY(vkCreateSampler(vk_device.logical, &samplerInfo, NULL, &vk_samplers[S_ANISO_NEAREST]));

	samplerInfo.maxLod = FLT_MAX;
	VK_VERIFY(vkCreateSampler(vk_device.logical, &samplerInfo, NULL, &vk_samplers[S_ANISO_MIPMAP_NEAREST]));

	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	VK_VERIFY(vkCreateSampler(vk_device.logical, &samplerInfo, NULL, &vk_samplers[S_ANISO_MIPMAP_LINEAR]));

	samplerInfo.maxLod = 1.f;
	VK_VERIFY(vkCreateSampler(vk_device.logical, &samplerInfo, NULL, &vk_samplers[S_ANISO_LINEAR]));
}

// internal helper
static void DestroySamplers()
{
	int i;
	for (i = 0; i < S_SAMPLER_CNT; ++i)
	{
		if (vk_samplers[i] != VK_NULL_HANDLE)
			vkDestroySampler(vk_device.logical, vk_samplers[i], NULL);

		vk_samplers[i] = VK_NULL_HANDLE;
	}
}

// internal helper
static void CreateDescriptorPool()
{
	VkDescriptorPoolSize poolSizes[] = {
		// UBO
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 16
		},
		// sampler
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_VKTEXTURES + 1
		},
		// input attachment
		{
			.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.descriptorCount = 1
		}
	};

	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = NULL,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = MAX_VKTEXTURES + 32,
		.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]),
		.pPoolSizes = poolSizes,
	};

	VK_VERIFY(vkCreateDescriptorPool(vk_device.logical, &poolInfo, NULL, &vk_descriptorPool));
}

// internal helper
static void CreateUboDescriptorSet(VkDescriptorSet *descSet, VkBuffer buffer)
{
	VkDescriptorSetAllocateInfo dsAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = NULL,
		.descriptorPool = vk_descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vk_uboDescSetLayout
	};

	VK_VERIFY(vkAllocateDescriptorSets(vk_device.logical, &dsAllocInfo, descSet));

	VkDescriptorBufferInfo bufferInfo = {
		.buffer = buffer,
		.offset = 0,
		.range = UNIFORM_ALLOC_SIZE
	};

	VkWriteDescriptorSet descriptorWrite = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = NULL,
		.dstSet = *descSet,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		.pImageInfo = NULL,
		.pBufferInfo = &bufferInfo,
		.pTexelBufferView = NULL,
	};

	vkUpdateDescriptorSets(vk_device.logical, 1, &descriptorWrite, 0, NULL);
}

// internal helper
static void CreateInputAttachmentDescriptorSet(VkDescriptorSet *descSet)
{

	VkDescriptorSetAllocateInfo dsAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = NULL,
		.descriptorPool = vk_descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vk_iaDescSetLayout
	};

	VK_VERIFY(vkAllocateDescriptorSets(vk_device.logical, &dsAllocInfo, descSet));

	VkDescriptorImageInfo imgInfo = {
		.sampler = VK_NULL_HANDLE,
		.imageView = vk_colorbufferWarp.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkWriteDescriptorSet descriptorWrite = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = NULL,
		.dstSet = *descSet,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
		.pImageInfo = &imgInfo,
		.pBufferInfo = NULL,
		.pTexelBufferView = NULL,
	};

	vkUpdateDescriptorSets(vk_device.logical, 1, &descriptorWrite, 0, NULL);
}

// internal helper
static void CreateDynamicBuffers()
{
	for (int i = 0; i < NUM_DYNBUFFERS; ++i)
	{
		QVk_CreateVertexBuffer(NULL, vk_config.vertex_buffer_size, &vk_dynVertexBuffers[i], NULL, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		QVk_CreateIndexBuffer(NULL, vk_config.index_buffer_size, &vk_dynIndexBuffers[i], NULL, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		QVk_CreateUniformBuffer(vk_config.uniform_buffer_size, &vk_dynUniformBuffers[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		// keep dynamic buffers persistently mapped
		vmaMapMemory(vk_malloc, vk_dynVertexBuffers[i].allocation, &vk_dynVertexBuffers[i].allocInfo.pMappedData);
		vmaMapMemory(vk_malloc, vk_dynIndexBuffers[i].allocation, &vk_dynIndexBuffers[i].allocInfo.pMappedData);
		vmaMapMemory(vk_malloc, vk_dynUniformBuffers[i].allocation, &vk_dynUniformBuffers[i].allocInfo.pMappedData);
		// create descriptor set for the uniform buffer
		CreateUboDescriptorSet(&vk_uboDescriptorSets[i], vk_dynUniformBuffers[i].buffer);
	}
}

// internal helper
static void ReleaseSwapBuffers()
{
	vk_activeSwapBufferIdx = (vk_activeSwapBufferIdx + 1) % NUM_SWAPBUFFER_SLOTS;
	int releaseBufferIdx   = (vk_activeSwapBufferIdx + 1) % NUM_SWAPBUFFER_SLOTS;

	if (vk_swapBuffersCnt[releaseBufferIdx] > 0)
	{
		for (int i = 0; i < vk_swapBuffersCnt[releaseBufferIdx]; i++)
			QVk_FreeBuffer(&vk_swapBuffers[releaseBufferIdx][i]);

		free(vk_swapBuffers[releaseBufferIdx]);
		vk_swapBuffers[releaseBufferIdx] = NULL;
		vk_swapBuffersCnt[releaseBufferIdx] = 0;
	}

	if (vk_swapDescSetsCnt[releaseBufferIdx] > 0)
	{
		vkFreeDescriptorSets(vk_device.logical, vk_descriptorPool, vk_swapDescSetsCnt[releaseBufferIdx], vk_swapDescriptorSets[releaseBufferIdx]);

		free(vk_swapDescriptorSets[releaseBufferIdx]);
		vk_swapDescriptorSets[releaseBufferIdx] = NULL;
		vk_swapDescSetsCnt[releaseBufferIdx] = 0;
	}
}

// internal helper
static int NextPow2(int v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

// internal helper
static void RebuildTriangleFanIndexBuffer()
{
	int idx = 0;
	VkDeviceSize dstOffset = 0;
	VkDeviceSize bufferSize = 3 * vk_config.triangle_fan_index_count * sizeof(uint16_t);
	uint16_t *iboData = NULL;
	uint16_t *fanData = malloc(bufferSize);

	// fill the index buffer so that we can emulate triangle fans via triangle lists
	for (int i = 0; i < vk_config.triangle_fan_index_count; ++i)
	{
		fanData[idx++] = 0;
		fanData[idx++] = i + 1;
		fanData[idx++] = i + 2;
	}

	for (int i = 0; i < NUM_DYNBUFFERS; ++i)
	{
		vk_activeDynBufferIdx = (vk_activeDynBufferIdx + 1) % NUM_DYNBUFFERS;
		vmaInvalidateAllocation(vk_malloc, vk_dynIndexBuffers[i].allocation, 0, VK_WHOLE_SIZE);

		iboData = (uint16_t *)QVk_GetIndexBuffer(bufferSize, &dstOffset);
		memcpy(iboData, fanData, bufferSize);

		vmaFlushAllocation(vk_malloc, vk_dynIndexBuffers[i].allocation, 0, VK_WHOLE_SIZE);
	}

	vk_triangleFanIbo = &vk_dynIndexBuffers[vk_activeDynBufferIdx].buffer;
	vk_triangleFanIboUsage = ((bufferSize % 4) == 0) ? bufferSize : (bufferSize + 4 - (bufferSize % 4));
	free(fanData);
}

// internal helper
static void CreateStagingBuffers()
{
	QVk_CreateCommandPool(&vk_stagingCommandPool, vk_device.gfxFamilyIndex);

	VkFenceCreateInfo fCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = 0
	};

	for (int i = 0; i < NUM_DYNBUFFERS; ++i)
	{
		QVk_CreateStagingBuffer(STAGING_BUFFER_MAXSIZE, &vk_stagingBuffers[i].buffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		vmaMapMemory(vk_malloc, vk_stagingBuffers[i].buffer.allocation, &vk_stagingBuffers[i].buffer.allocInfo.pMappedData);
		vk_stagingBuffers[i].submitted = false;

		VK_VERIFY(vkCreateFence(vk_device.logical, &fCreateInfo, NULL, &vk_stagingBuffers[i].fence));

		vk_stagingBuffers[i].cmdBuffer = QVk_CreateCommandBuffer(&vk_stagingCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		VK_VERIFY(QVk_BeginCommand(&vk_stagingBuffers[i].cmdBuffer));
	}
}

// internal helper
static void SubmitStagingBuffer(int index)
{
	VkMemoryBarrier memBarrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.pNext = NULL,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
	};

	vkCmdPipelineBarrier(vk_stagingBuffers[index].cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &memBarrier, 0, NULL, 0, NULL);
	vkEndCommandBuffer(vk_stagingBuffers[index].cmdBuffer);

	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = NULL,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = NULL,
		.pWaitDstStageMask = NULL,
		.commandBufferCount = 1,
		.pCommandBuffers = &vk_stagingBuffers[index].cmdBuffer
	};

	vkQueueSubmit(vk_device.gfxQueue, 1, &submitInfo, vk_stagingBuffers[index].fence);

	vk_stagingBuffers[index].submitted = true;
	vk_activeStagingBuffer = (vk_activeStagingBuffer + 1) % NUM_DYNBUFFERS;
}

// internal helper
static void CreateStaticBuffers()
{
	const float texVerts[] = {	-1., -1., 0., 0.,
								 1.,  1., 1., 1.,
								-1.,  1., 0., 1.,
								 1., -1., 1., 0. };

	const float colorVerts[] = { -1., -1.,
								  1.,  1.,
								 -1.,  1.,
								  1., -1. };

	const uint32_t indices[] = { 0, 1, 2, 0, 3, 1 };

	QVk_CreateVertexBuffer(texVerts, sizeof(texVerts), &vk_texRectVbo, NULL, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	QVk_CreateVertexBuffer(colorVerts, sizeof(colorVerts), &vk_colorRectVbo, NULL, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	QVk_CreateIndexBuffer(indices, sizeof(indices), &vk_rectIbo, NULL, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
}

// internal helper
static void CreatePipelines()
{
	// shared pipeline vertex input state create infos
	VK_VERTINFO(RG, sizeof(float) * 2, VK_INPUTATTR_DESC(0, VK_FORMAT_R32G32_SFLOAT, 0));

	VK_VERTINFO(RGB, sizeof(float) * 3, VK_INPUTATTR_DESC(0, VK_FORMAT_R32G32B32_SFLOAT, 0));

	VK_VERTINFO(RG_RG, sizeof(float) * 4,	VK_INPUTATTR_DESC(0, VK_FORMAT_R32G32_SFLOAT, 0),
											VK_INPUTATTR_DESC(1, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2));

	VK_VERTINFO(RGB_RG, sizeof(float) * 5,	VK_INPUTATTR_DESC(0, VK_FORMAT_R32G32B32_SFLOAT, 0), 
											VK_INPUTATTR_DESC(1, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 3));

	VK_VERTINFO(RGB_RGB, sizeof(float) * 6,	VK_INPUTATTR_DESC(0, VK_FORMAT_R32G32B32_SFLOAT, 0),
											VK_INPUTATTR_DESC(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3));

	VK_VERTINFO(RGB_RGBA,  sizeof(float) * 7,	VK_INPUTATTR_DESC(0, VK_FORMAT_R32G32B32_SFLOAT, 0),
												VK_INPUTATTR_DESC(1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 3));

	VK_VERTINFO(RGB_RG_RG, sizeof(float) * 7,	VK_INPUTATTR_DESC(0, VK_FORMAT_R32G32B32_SFLOAT, 0),
												VK_INPUTATTR_DESC(1, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 3),
												VK_INPUTATTR_DESC(2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 5));

	VK_VERTINFO(RGB_RGBA_RG, sizeof(float) * 9,	VK_INPUTATTR_DESC(0, VK_FORMAT_R32G32B32_SFLOAT, 0),
												VK_INPUTATTR_DESC(1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 3),
												VK_INPUTATTR_DESC(2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 7));

	// shared descriptor set layouts
	VkDescriptorSetLayout samplerUboDsLayouts[] = { vk_samplerDescSetLayout, vk_uboDescSetLayout };
	VkDescriptorSetLayout samplerUboLmapDsLayouts[] = { vk_samplerDescSetLayout, vk_uboDescSetLayout, vk_samplerLightmapDescSetLayout };
	VkDescriptorSetLayout foosets[] = { vk_samplerDescSetLayout, vk_iaDescSetLayout };
	// shader array (vertex and fragment, no compute... yet)
	qvkshader_t shaders[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

	// we'll be using some push constants in vertex shaders
	// size accomodates for maximum number of uploaded elements (should probably be checked against the hardware's maximum supported value)
	VkPushConstantRange pushConstantRange = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = 32 * sizeof(float)
	};

	// textured quad pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, basic, basic);
	vk_drawTexQuadPipeline.depthTestEnable = VK_FALSE;
	QVk_CreatePipeline(samplerUboDsLayouts, 2, &vertInfoRG_RG, &vk_drawTexQuadPipeline, &vk_renderpasses[RP_UI], shaders, 2, &pushConstantRange);

	// draw particles pipeline (using a texture)
	VK_LOAD_VERTFRAG_SHADERS(shaders, particle, basic);
	vk_drawParticlesPipeline.depthWriteEnable = VK_FALSE;
	vk_drawParticlesPipeline.blendOpts.blendEnable = VK_TRUE;
	QVk_CreatePipeline(&vk_samplerDescSetLayout, 1, &vertInfoRGB_RGBA_RG, &vk_drawParticlesPipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// draw particles pipeline (using point list)
	VK_LOAD_VERTFRAG_SHADERS(shaders, point_particle, point_particle);
	vk_drawPointParticlesPipeline.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	vk_drawPointParticlesPipeline.depthWriteEnable = VK_FALSE;
	vk_drawPointParticlesPipeline.blendOpts.blendEnable = VK_TRUE;
	QVk_CreatePipeline(&vk_uboDescSetLayout, 1, &vertInfoRGB_RGBA, &vk_drawPointParticlesPipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// colored quad pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, basic_color_quad, basic_color_quad);
	for (int i = 0; i < 2; ++i)
	{
		vk_drawColorQuadPipeline[i].depthTestEnable = VK_FALSE;
		vk_drawColorQuadPipeline[i].blendOpts.blendEnable = VK_TRUE;
		QVk_CreatePipeline(&vk_uboDescSetLayout, 1, &vertInfoRG, &vk_drawColorQuadPipeline[i], &vk_renderpasses[i], shaders, 2, &pushConstantRange);
	}

	// untextured null model
	VK_LOAD_VERTFRAG_SHADERS(shaders, nullmodel, basic_color_quad);
	vk_drawNullModel.cullMode = VK_CULL_MODE_NONE;
	vk_drawNullModel.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	QVk_CreatePipeline(&vk_uboDescSetLayout, 1, &vertInfoRGB_RGB, &vk_drawNullModel, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// textured model
	VK_LOAD_VERTFRAG_SHADERS(shaders, model, model);
	for (int i = 0; i < 2; ++i)
	{
		vk_drawModelPipelineStrip[i].topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		vk_drawModelPipelineStrip[i].blendOpts.blendEnable = VK_TRUE;
		QVk_CreatePipeline(samplerUboDsLayouts, 2, &vertInfoRGB_RGBA_RG, &vk_drawModelPipelineStrip[i], &vk_renderpasses[i], shaders, 2, &pushConstantRange);

		vk_drawModelPipelineFan[i].topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		vk_drawModelPipelineFan[i].blendOpts.blendEnable = VK_TRUE;
		QVk_CreatePipeline(samplerUboDsLayouts, 2, &vertInfoRGB_RGBA_RG, &vk_drawModelPipelineFan[i], &vk_renderpasses[i], shaders, 2, &pushConstantRange);
	}

	// dedicated model pipelines for translucent objects with depth write disabled
	vk_drawNoDepthModelPipelineStrip.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	vk_drawNoDepthModelPipelineStrip.depthWriteEnable = VK_FALSE;
	vk_drawNoDepthModelPipelineStrip.blendOpts.blendEnable = VK_TRUE;
	QVk_CreatePipeline(samplerUboDsLayouts, 2, &vertInfoRGB_RGBA_RG, &vk_drawNoDepthModelPipelineStrip, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	vk_drawNoDepthModelPipelineFan.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	vk_drawNoDepthModelPipelineFan.depthWriteEnable = VK_FALSE;
	vk_drawNoDepthModelPipelineFan.blendOpts.blendEnable = VK_TRUE;
	QVk_CreatePipeline(samplerUboDsLayouts, 2, &vertInfoRGB_RGBA_RG, &vk_drawNoDepthModelPipelineFan, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// dedicated model pipelines for when left-handed weapon model is drawn
	vk_drawLefthandModelPipelineStrip.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	vk_drawLefthandModelPipelineStrip.cullMode = VK_CULL_MODE_FRONT_BIT;
	QVk_CreatePipeline(samplerUboDsLayouts, 2, &vertInfoRGB_RGBA_RG, &vk_drawLefthandModelPipelineStrip, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	vk_drawLefthandModelPipelineFan.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	vk_drawLefthandModelPipelineFan.cullMode = VK_CULL_MODE_FRONT_BIT;
	QVk_CreatePipeline(samplerUboDsLayouts, 2, &vertInfoRGB_RGBA_RG, &vk_drawLefthandModelPipelineFan, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// draw sprite pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, sprite, basic);
	vk_drawSpritePipeline.blendOpts.blendEnable = VK_TRUE;
	QVk_CreatePipeline(&vk_samplerDescSetLayout, 1, &vertInfoRGB_RG, &vk_drawSpritePipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// draw polygon pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, polygon, basic);
	vk_drawPolyPipeline.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	vk_drawPolyPipeline.blendOpts.blendEnable = VK_TRUE;
	QVk_CreatePipeline(samplerUboDsLayouts, 2, &vertInfoRGB_RG, &vk_drawPolyPipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// draw lightmapped polygon
	VK_LOAD_VERTFRAG_SHADERS(shaders, polygon_lmap, polygon_lmap);
	vk_drawPolyLmapPipeline.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	QVk_CreatePipeline(samplerUboLmapDsLayouts, 3, &vertInfoRGB_RG_RG, &vk_drawPolyLmapPipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// draw polygon with warp effect (liquid) pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, polygon_warp, basic);
	vk_drawPolyWarpPipeline.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	vk_drawPolyWarpPipeline.blendOpts.blendEnable = VK_TRUE;
	QVk_CreatePipeline(samplerUboLmapDsLayouts, 2, &vertInfoRGB_RG, &vk_drawPolyWarpPipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// draw beam pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, beam, basic_color_quad);
	vk_drawBeamPipeline.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	vk_drawBeamPipeline.depthWriteEnable = VK_FALSE;
	vk_drawBeamPipeline.blendOpts.blendEnable = VK_TRUE;
	QVk_CreatePipeline(&vk_uboDescSetLayout, 1, &vertInfoRGB, &vk_drawBeamPipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// draw skybox pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, skybox, basic);
	QVk_CreatePipeline(samplerUboDsLayouts, 2, &vertInfoRGB_RG, &vk_drawSkyboxPipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// draw dynamic light pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, d_light, basic_color_quad);
	vk_drawDLightPipeline.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	vk_drawDLightPipeline.depthWriteEnable = VK_FALSE;
	vk_drawDLightPipeline.cullMode = VK_CULL_MODE_FRONT_BIT;
	vk_drawDLightPipeline.blendOpts.blendEnable = VK_TRUE;
	vk_drawDLightPipeline.blendOpts.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	vk_drawDLightPipeline.blendOpts.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	vk_drawDLightPipeline.blendOpts.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	vk_drawDLightPipeline.blendOpts.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	QVk_CreatePipeline(&vk_uboDescSetLayout, 1, &vertInfoRGB_RGB, &vk_drawDLightPipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// vk_showtris render pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, d_light, basic_color_quad);
	vk_showTrisPipeline.cullMode = VK_CULL_MODE_NONE;
	vk_showTrisPipeline.depthTestEnable = VK_FALSE;
	vk_showTrisPipeline.depthWriteEnable = VK_FALSE;
	vk_showTrisPipeline.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
	QVk_CreatePipeline(&vk_uboDescSetLayout, 1, &vertInfoRGB_RGB, &vk_showTrisPipeline, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	//vk_shadows render pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, shadows, basic_color_quad);
	vk_shadowsPipelineStrip.blendOpts.blendEnable = VK_TRUE;
	vk_shadowsPipelineStrip.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	QVk_CreatePipeline(&vk_uboDescSetLayout, 1, &vertInfoRGB, &vk_shadowsPipelineStrip, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	vk_shadowsPipelineFan.blendOpts.blendEnable = VK_TRUE;
	vk_shadowsPipelineFan.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	QVk_CreatePipeline(&vk_uboDescSetLayout, 1, &vertInfoRGB, &vk_shadowsPipelineFan, &vk_renderpasses[RP_WORLD], shaders, 2, &pushConstantRange);

	// postprocessing pipeline
	VK_LOAD_VERTFRAG_SHADERS(shaders, postprocess, postprocess);

	VkPipelineVertexInputStateCreateInfo postprocessVisc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.vertexBindingDescriptionCount = 0,
		.pVertexBindingDescriptions = NULL,
		.vertexAttributeDescriptionCount = 0,
		.pVertexAttributeDescriptions = NULL,
	};

	vk_postprocessPipeline.depthTestEnable = VK_FALSE;
	vk_postprocessPipeline.depthWriteEnable = VK_FALSE;
	vk_postprocessPipeline.cullMode = VK_CULL_MODE_NONE;
	vk_postprocessPipeline.subpass = 1;
	QVk_CreatePipeline(foosets, 2, &postprocessVisc, &vk_postprocessPipeline, &vk_renderpasses[RP_UI], shaders, 2, NULL);

	VK_LOAD_VERTFRAG_SHADERS(shaders, world_warp, world_warp);
	vk_worldWarpPipeline.depthTestEnable = VK_FALSE;
	vk_worldWarpPipeline.depthWriteEnable = VK_FALSE;
	vk_worldWarpPipeline.cullMode = VK_CULL_MODE_NONE;
	QVk_CreatePipeline(foosets, 2, &postprocessVisc, &vk_worldWarpPipeline, &vk_renderpasses[RP_WORLD_WARP], shaders, 2, &pushConstantRange);

	// final shader cleanup
	vkDestroyShaderModule(vk_device.logical, shaders[0].module, NULL);
	vkDestroyShaderModule(vk_device.logical, shaders[1].module, NULL);
}

/*
** QVk_Shutdown
**
** Destroy all Vulkan related resources.
*/
void QVk_Shutdown( void )
{
	if (vk_instance != VK_NULL_HANDLE)
	{
		ri.Con_Printf(PRINT_ALL, "Shutting down Vulkan\n");

		QVk_DestroyPipeline(&vk_drawTexQuadPipeline);
		QVk_DestroyPipeline(&vk_drawNullModel);
		QVk_DestroyPipeline(&vk_drawColorQuadPipeline[RP_WORLD]);
		QVk_DestroyPipeline(&vk_drawModelPipelineStrip[RP_WORLD]);
		QVk_DestroyPipeline(&vk_drawModelPipelineFan[RP_WORLD]);
		QVk_DestroyPipeline(&vk_drawColorQuadPipeline[RP_UI]);
		QVk_DestroyPipeline(&vk_drawModelPipelineStrip[RP_UI]);
		QVk_DestroyPipeline(&vk_drawModelPipelineFan[RP_UI]);
		QVk_DestroyPipeline(&vk_drawNoDepthModelPipelineStrip);
		QVk_DestroyPipeline(&vk_drawNoDepthModelPipelineFan);
		QVk_DestroyPipeline(&vk_drawLefthandModelPipelineStrip);
		QVk_DestroyPipeline(&vk_drawLefthandModelPipelineFan);
		QVk_DestroyPipeline(&vk_drawParticlesPipeline);
		QVk_DestroyPipeline(&vk_drawPointParticlesPipeline);
		QVk_DestroyPipeline(&vk_drawSpritePipeline);
		QVk_DestroyPipeline(&vk_drawPolyPipeline);
		QVk_DestroyPipeline(&vk_drawPolyLmapPipeline);
		QVk_DestroyPipeline(&vk_drawPolyWarpPipeline);
		QVk_DestroyPipeline(&vk_drawBeamPipeline);
		QVk_DestroyPipeline(&vk_drawSkyboxPipeline);
		QVk_DestroyPipeline(&vk_drawDLightPipeline);
		QVk_DestroyPipeline(&vk_showTrisPipeline);
		QVk_DestroyPipeline(&vk_shadowsPipelineStrip);
		QVk_DestroyPipeline(&vk_shadowsPipelineFan);
		QVk_DestroyPipeline(&vk_postprocessPipeline);
		QVk_DestroyPipeline(&vk_worldWarpPipeline);
		QVk_FreeBuffer(&vk_texRectVbo);
		QVk_FreeBuffer(&vk_colorRectVbo);
		QVk_FreeBuffer(&vk_rectIbo);
		for (int i = 0; i < NUM_DYNBUFFERS; ++i)
		{
			if (vk_dynUniformBuffers[i].buffer != VK_NULL_HANDLE)
			{
				vmaUnmapMemory(vk_malloc, vk_dynUniformBuffers[i].allocation);
				QVk_FreeBuffer(&vk_dynUniformBuffers[i]);
			}
			if (vk_dynIndexBuffers[i].buffer != VK_NULL_HANDLE)
			{
				vmaUnmapMemory(vk_malloc, vk_dynIndexBuffers[i].allocation);
				QVk_FreeBuffer(&vk_dynIndexBuffers[i]);
			}
			if (vk_dynVertexBuffers[i].buffer != VK_NULL_HANDLE)
			{
				vmaUnmapMemory(vk_malloc, vk_dynVertexBuffers[i].allocation);
				QVk_FreeBuffer(&vk_dynVertexBuffers[i]);
			}
			if (vk_stagingBuffers[i].buffer.buffer != VK_NULL_HANDLE)
			{
				vmaUnmapMemory(vk_malloc, vk_stagingBuffers[i].buffer.allocation);
				QVk_FreeBuffer(&vk_stagingBuffers[i].buffer);
				vkDestroyFence(vk_device.logical, vk_stagingBuffers[i].fence, NULL);
			}
		}
		if (vk_descriptorPool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(vk_device.logical, vk_descriptorPool, NULL);
		if (vk_uboDescSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(vk_device.logical, vk_uboDescSetLayout, NULL);
		if (vk_samplerDescSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(vk_device.logical, vk_samplerDescSetLayout, NULL);
		if (vk_samplerLightmapDescSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(vk_device.logical, vk_samplerLightmapDescSetLayout, NULL);
		if (vk_iaDescSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(vk_device.logical, vk_iaDescSetLayout, NULL);
		for (int i = 0; i < RP_COUNT; i++)
		{
			if (vk_renderpasses[i].rp != VK_NULL_HANDLE)
				vkDestroyRenderPass(vk_device.logical, vk_renderpasses[i].rp, NULL);
			vk_renderpasses[i].rp = VK_NULL_HANDLE;
		}
		if (vk_commandbuffers)
		{
			vkFreeCommandBuffers(vk_device.logical, vk_commandPool, NUM_CMDBUFFERS, vk_commandbuffers);
			free(vk_commandbuffers);
			vk_commandbuffers = NULL;
		}
		if (vk_commandPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(vk_device.logical, vk_commandPool, NULL);
		if (vk_transferCommandPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(vk_device.logical, vk_transferCommandPool, NULL);
		if (vk_stagingCommandPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(vk_device.logical, vk_stagingCommandPool, NULL);
		DestroySamplers();
		DestroyFramebuffers();
		DestroyImageViews();
		DestroyDrawBuffers();
		if (vk_swapchain.sc != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(vk_device.logical, vk_swapchain.sc, NULL);
			free(vk_swapchain.images);
			vk_swapchain.sc = VK_NULL_HANDLE;
			vk_swapchain.images = NULL;
			vk_swapchain.imageCount = 0;
		}
		for (int i = 0; i < NUM_CMDBUFFERS; ++i)
		{
			vkDestroySemaphore(vk_device.logical, vk_imageAvailableSemaphores[i], NULL);
			vkDestroySemaphore(vk_device.logical, vk_renderFinishedSemaphores[i], NULL);
			vkDestroyFence(vk_device.logical, vk_fences[i], NULL);
		}
		if (vk_malloc != VK_NULL_HANDLE)
			vmaDestroyAllocator(vk_malloc);
		if (vk_device.logical != VK_NULL_HANDLE)
			vkDestroyDevice(vk_device.logical, NULL);
		if(vk_surface != VK_NULL_HANDLE)
			vkDestroySurfaceKHR(vk_instance, vk_surface, NULL);
		QVk_DestroyValidationLayers();

		vkDestroyInstance(vk_instance, NULL);
		vk_instance = VK_NULL_HANDLE;
		vk_activeCmdbuffer = VK_NULL_HANDLE;
		vk_descriptorPool = VK_NULL_HANDLE;
		vk_uboDescSetLayout = VK_NULL_HANDLE;
		vk_samplerDescSetLayout = VK_NULL_HANDLE;
		vk_samplerLightmapDescSetLayout = VK_NULL_HANDLE;
		vk_commandPool = VK_NULL_HANDLE;
		vk_transferCommandPool = VK_NULL_HANDLE;
		vk_stagingCommandPool = VK_NULL_HANDLE;
		vk_activeBufferIdx = 0;
		vk_imageIndex = 0;
	}
}

#	pragma warning (disable : 4113 4133 4047 )

/*
** QVk_Init
**
** This is responsible for initializing Vulkan.
** 
*/
qboolean QVk_Init()
{
	PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
	uint32_t instanceVersion = VK_API_VERSION_1_0;

	if (vkEnumerateInstanceVersion)
	{
		VK_VERIFY(vkEnumerateInstanceVersion(&instanceVersion));
	}

	VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pNext = NULL,
		.pApplicationName = "Quake 2",
		.applicationVersion = VK_MAKE_VERSION(3, 21, 0),
		.pEngineName = "id Tech 2",
		.engineVersion = VK_MAKE_VERSION(2, 0, 0),
		.apiVersion = instanceVersion
	};

	uint32_t extCount;
	char **wantedExtensions;
	memset((char*)vk_config.supported_present_modes, 0, 256);
	memset((char*)vk_config.extensions, 0, 256);
	memset((char*)vk_config.layers, 0, 256);
	vk_config.vk_version = instanceVersion;
	vk_config.vertex_buffer_usage  = 0;
	vk_config.vertex_buffer_max_usage = 0;
	vk_config.vertex_buffer_size   = VERTEX_BUFFER_SIZE;
	vk_config.index_buffer_usage   = 0;
	vk_config.index_buffer_max_usage = 0;
	vk_config.index_buffer_size    = INDEX_BUFFER_SIZE;
	vk_config.uniform_buffer_usage = 0;
	vk_config.uniform_buffer_max_usage = 0;
	vk_config.uniform_buffer_size  = UNIFORM_BUFFER_SIZE;
	vk_config.triangle_fan_index_usage = 0;
	vk_config.triangle_fan_index_max_usage = 0;
	vk_config.triangle_fan_index_count = TRIANGLE_FAN_INDEX_CNT;

	Vkimp_GetSurfaceExtensions(NULL, &extCount);

	if (vk_validation->value)
		extCount++;

	wantedExtensions = (char **)malloc(extCount * sizeof(const char *));
	Vkimp_GetSurfaceExtensions(wantedExtensions, NULL);

	if (vk_validation->value)
		wantedExtensions[extCount - 1] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

	ri.Con_Printf(PRINT_ALL, "Enabled extensions: ");
	for (int i = 0; i < extCount; i++)
	{
		ri.Con_Printf(PRINT_ALL, "%s ", wantedExtensions[i]);
		vk_config.extensions[i] = wantedExtensions[i];
	}
	ri.Con_Printf(PRINT_ALL, "\n");

	VkInstanceCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = NULL,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL,
		.enabledExtensionCount = extCount,
		.ppEnabledExtensionNames = (const char* const*)wantedExtensions
	};

#if VK_HEADER_VERSION > 101
	const char *validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
#else
	const char *validationLayers[] = { "VK_LAYER_LUNARG_standard_validation" };
#endif

	if (vk_validation->value)
	{
		createInfo.enabledLayerCount = sizeof(validationLayers) / sizeof(validationLayers[0]);
		createInfo.ppEnabledLayerNames = validationLayers;
		for (int i = 0; i < createInfo.enabledLayerCount; i++)
		{
			vk_config.layers[i] = validationLayers[i];
		}
	}

	VkResult res = vkCreateInstance(&createInfo, NULL, &vk_instance);
	free(wantedExtensions);

	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan instance: %s\n", QVk_GetError(res));
		return false;
	}
	ri.Con_Printf(PRINT_ALL, "...created Vulkan instance\n");

	// initialize function pointers
	qvkCreateDebugUtilsMessengerEXT  = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk_instance, "vkCreateDebugUtilsMessengerEXT");
	qvkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk_instance, "vkDestroyDebugUtilsMessengerEXT");

	if (vk_validation->value)
		QVk_CreateValidationLayers();

	res = Vkimp_CreateSurface();
	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan surface: %s\n", QVk_GetError(res));
		return false;
	}
	ri.Con_Printf(PRINT_ALL, "...created Vulkan surface\n");

	// create Vulkan device - see if the user prefers any specific device if there's more than one GPU in the system
	QVk_CreateDevice((int)vk_device_idx->value);
	// create memory allocator
	VmaAllocatorCreateInfo allocInfo = {
		.flags = 0,
		.physicalDevice = vk_device.physical,
		.device = vk_device.logical,
		.preferredLargeHeapBlockSize = 0,
		.pAllocationCallbacks = NULL,
		.pDeviceMemoryCallbacks = NULL,
		.frameInUseCount = 0,
		.pHeapSizeLimit = NULL,
		.pVulkanFunctions = NULL,
		.pRecordSettings = NULL
	};

	res = vmaCreateAllocator(&allocInfo, &vk_malloc);
	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan memory allocator: %s\n", QVk_GetError(res));
		return false;
	}
	ri.Con_Printf(PRINT_ALL, "...created Vulkan memory allocator\n");

	// setup swapchain
	res = QVk_CreateSwapchain();
	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan swapchain: %s\n", QVk_GetError(res));
		return false;
	}
	ri.Con_Printf(PRINT_ALL, "...created Vulkan swapchain\n");

	// set viewport and scissor
	vk_viewport.x = 0.f;
	vk_viewport.y = 0.f;
	vk_viewport.minDepth = 0.f;
	vk_viewport.maxDepth = 1.f;
	vk_viewport.width = (float)vid.width;
	vk_viewport.height = (float)vid.height;
	vk_scissor.offset.x = 0;
	vk_scissor.offset.y = 0;
	vk_scissor.extent = vk_swapchain.extent;

	// setup fences and semaphores
	VkFenceCreateInfo fCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = NULL,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};
	VkSemaphoreCreateInfo sCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};
	for (int i = 0; i < NUM_CMDBUFFERS; ++i)
	{
		VK_VERIFY(vkCreateFence(vk_device.logical, &fCreateInfo, NULL, &vk_fences[i]));
		VK_VERIFY(vkCreateSemaphore(vk_device.logical, &sCreateInfo, NULL, &vk_imageAvailableSemaphores[i]));
		VK_VERIFY(vkCreateSemaphore(vk_device.logical, &sCreateInfo, NULL, &vk_renderFinishedSemaphores[i]));
	}
	ri.Con_Printf(PRINT_ALL, "...created synchronization objects\n");

	// setup render passes
	for (int i = 0; i < RP_COUNT; ++i)
	{
		vk_renderpasses[i].colorLoadOp = vk_clear->value ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}

	VkSampleCountFlagBits msaaMode = GetSampleCount();
	VkSampleCountFlagBits supportedMsaa = vk_device.properties.limits.framebufferColorSampleCounts;
	if (!(supportedMsaa & msaaMode))
	{
		ri.Con_Printf(PRINT_ALL, "MSAAx%d mode not supported, aborting...\n", msaaMode);
		ri.Cvar_Set("vk_msaa", "0");
		msaaMode = VK_SAMPLE_COUNT_1_BIT;
		// avoid secondary video reload
		vk_msaa->modified = false;
	}

	vk_renderpasses[RP_WORLD].sampleCount = msaaMode;

	// setup render passes
	res = CreateRenderpasses();
	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan render passes: %s\n", QVk_GetError(res));
		return false;
	}

	// setup command pools
	res = QVk_CreateCommandPool(&vk_commandPool, vk_device.gfxFamilyIndex);
	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan command pool for graphics: %s\n", QVk_GetError(res));
		return false;
	}
	res = QVk_CreateCommandPool(&vk_transferCommandPool, vk_device.transferFamilyIndex);
	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan command pool for transfer: %s\n", QVk_GetError(res));
		return false;
	}
	ri.Con_Printf(PRINT_ALL, "...created Vulkan command pools\n");

	// setup draw buffers
	CreateDrawBuffers();

	// setup image views
	res = CreateImageViews();
	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan image views: %s\n", QVk_GetError(res));
		return false;
	}
	ri.Con_Printf(PRINT_ALL, "...created %d Vulkan image view(s)\n", vk_swapchain.imageCount);

	// setup framebuffers
	res = CreateFramebuffers();
	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan framebuffers: %s\n", QVk_GetError(res));
		return false;
	}
	ri.Con_Printf(PRINT_ALL, "...created %d Vulkan framebuffers\n", vk_swapchain.imageCount);

	// setup command buffers (double buffering)
	vk_commandbuffers = (VkCommandBuffer *)malloc(NUM_CMDBUFFERS * sizeof(VkCommandBuffer));

	VkCommandBufferAllocateInfo cbInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = NULL,
		.commandPool = vk_commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = NUM_CMDBUFFERS
	};

	res = vkAllocateCommandBuffers(vk_device.logical, &cbInfo, vk_commandbuffers);
	if (res != VK_SUCCESS)
	{
		ri.Con_Printf(PRINT_ALL, "QVk_Init(): Could not create Vulkan commandbuffers: %s\n", QVk_GetError(res));
		free(vk_commandbuffers);
		vk_commandbuffers = NULL;
		return false;
	}
	ri.Con_Printf(PRINT_ALL, "...created %d Vulkan commandbuffers\n", NUM_CMDBUFFERS);

	// initialize tracker variables
	vk_activeCmdbuffer   = vk_commandbuffers[vk_activeBufferIdx];

	CreateDescriptorSetLayouts();
	CreateDescriptorPool();
	// create static vertex/index buffers reused in the games
	CreateStaticBuffers();
	// create vertex, index and uniform buffer pools
	CreateDynamicBuffers();
	// create staging buffers
	CreateStagingBuffers();
	// assign a dynamic index buffer for triangle fan emulation
	RebuildTriangleFanIndexBuffer();
	CreateInputAttachmentDescriptorSet(&vk_iaDescriptorSet);
	CreatePipelines();
	CreateSamplers();

	// create descriptor set for the texture
	VkDescriptorSetAllocateInfo dsAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = NULL,
		.descriptorPool = vk_descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vk_samplerDescSetLayout
	};

	VK_VERIFY(vkAllocateDescriptorSets(vk_device.logical, &dsAllocInfo, &vk_colorbuffer.descriptorSet));
	QVk_UpdateTextureSampler(&vk_colorbuffer, 0);
	VK_VERIFY(vkAllocateDescriptorSets(vk_device.logical, &dsAllocInfo, &vk_colorbufferWarp.descriptorSet));
	QVk_UpdateTextureSampler(&vk_colorbufferWarp, 0);

	return true;
}

VkResult QVk_BeginFrame()
{
	// reset tracking variables
	vk_state.current_pipeline = VK_NULL_HANDLE;
	vk_config.vertex_buffer_usage  = 0;
	// triangle fan index buffer data will not be cleared between frames unless the buffer itself is too small
	vk_config.index_buffer_usage   = vk_triangleFanIboUsage;
	vk_config.uniform_buffer_usage = 0;
	vk_config.triangle_fan_index_usage = 0;

	ReleaseSwapBuffers();

	VkResult result = vkAcquireNextImageKHR(vk_device.logical, vk_swapchain.sc, UINT32_MAX, vk_imageAvailableSemaphores[vk_activeBufferIdx], VK_NULL_HANDLE, &vk_imageIndex);
	vk_activeCmdbuffer = vk_commandbuffers[vk_activeBufferIdx];

	// swap dynamic buffers
	vk_activeDynBufferIdx = (vk_activeDynBufferIdx + 1) % NUM_DYNBUFFERS;
	vk_dynUniformBuffers[vk_activeDynBufferIdx].currentOffset = 0;
	vk_dynVertexBuffers[vk_activeDynBufferIdx].currentOffset = 0;
	// triangle fan index data is placed in the beginning of the buffer
	vk_dynIndexBuffers[vk_activeDynBufferIdx].currentOffset = vk_triangleFanIboUsage;
	vmaInvalidateAllocation(vk_malloc, vk_dynUniformBuffers[vk_activeDynBufferIdx].allocation, 0, VK_WHOLE_SIZE);
	vmaInvalidateAllocation(vk_malloc, vk_dynVertexBuffers[vk_activeDynBufferIdx].allocation, 0, VK_WHOLE_SIZE);
	vmaInvalidateAllocation(vk_malloc, vk_dynIndexBuffers[vk_activeDynBufferIdx].allocation, 0, VK_WHOLE_SIZE);

	// swapchain has become incompatible - need to recreate it
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		ri.Con_Printf(PRINT_ALL, "Vulkan swapchain incompatible after vkAcquireNextImageKHR - rebuilding!\n");
		QVk_RecreateSwapchain();
		return result;
	}

	VK_VERIFY(vkWaitForFences(vk_device.logical, 1, &vk_fences[vk_activeBufferIdx], VK_TRUE, UINT32_MAX));
	VK_VERIFY(vkResetFences(vk_device.logical, 1, &vk_fences[vk_activeBufferIdx]));

	assert((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && "Could not acquire swapchain image!");

	// setup command buffers and render pass for drawing
	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = NULL,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = NULL
	};

	VK_VERIFY(vkBeginCommandBuffer(vk_commandbuffers[vk_activeBufferIdx], &beginInfo));

	vkCmdSetViewport(vk_commandbuffers[vk_activeBufferIdx], 0, 1, &vk_viewport);
	vkCmdSetScissor(vk_commandbuffers[vk_activeBufferIdx], 0, 1, &vk_scissor);

	vk_frameStarted = true;
	return VK_SUCCESS;
}

VkResult QVk_EndFrame(qboolean force)
{
	// continue only if QVk_BeginFrame() had been previously issued
	if (!vk_frameStarted)
		return VK_NOT_READY;
	// this may happen if Sys_Error is issued mid-frame, so we need to properly advance the draw pipeline
	if (force)
	{
		extern void R_EndWorldRenderpass(void);
		R_EndWorldRenderpass();
	}

	// submit
	QVk_SubmitStagingBuffers();
	vmaFlushAllocation(vk_malloc, vk_dynUniformBuffers[vk_activeDynBufferIdx].allocation, 0, VK_WHOLE_SIZE);
	vmaFlushAllocation(vk_malloc, vk_dynVertexBuffers[vk_activeDynBufferIdx].allocation, 0, VK_WHOLE_SIZE);
	vmaFlushAllocation(vk_malloc, vk_dynIndexBuffers[vk_activeDynBufferIdx].allocation, 0, VK_WHOLE_SIZE);

	vkCmdNextSubpass(vk_commandbuffers[vk_activeBufferIdx], VK_SUBPASS_CONTENTS_INLINE);
	VkDescriptorSet sets[] = { vk_colorbufferWarp.descriptorSet, vk_iaDescriptorSet };
	vkCmdBindDescriptorSets(vk_commandbuffers[vk_activeBufferIdx], VK_PIPELINE_BIND_POINT_GRAPHICS, vk_postprocessPipeline.layout, 0, 2, sets, 0, NULL);
	QVk_BindPipeline(&vk_postprocessPipeline);
	vkCmdDraw(vk_commandbuffers[vk_activeBufferIdx], 3, 1, 0, 0);
	vkCmdEndRenderPass(vk_commandbuffers[vk_activeBufferIdx]);
	VK_VERIFY(vkEndCommandBuffer(vk_commandbuffers[vk_activeBufferIdx]));

	VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vk_imageAvailableSemaphores[vk_activeBufferIdx],
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &vk_renderFinishedSemaphores[vk_activeBufferIdx],
		.pWaitDstStageMask = &waitStages,
		.commandBufferCount = 1,
		.pCommandBuffers = &vk_commandbuffers[vk_activeBufferIdx]
	};

	VK_VERIFY(vkQueueSubmit(vk_device.gfxQueue, 1, &submitInfo, vk_fences[vk_activeBufferIdx]));

	// present
	VkPresentInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vk_renderFinishedSemaphores[vk_activeBufferIdx],
		.swapchainCount = 1,
		.pSwapchains = &vk_swapchain.sc,
		.pImageIndices = &vk_imageIndex,
		.pResults = NULL
	};

	VkResult renderResult = vkQueuePresentKHR(vk_device.presentQueue, &presentInfo);

	// recreate swapchain if it's out of date
	if (renderResult == VK_ERROR_OUT_OF_DATE_KHR || renderResult == VK_SUBOPTIMAL_KHR)
	{
		ri.Con_Printf(PRINT_ALL, "Vulkan swapchain out of date/suboptimal after vkQueuePresentKHR - rebuilding!\n");
		QVk_RecreateSwapchain();
	}

	vk_activeBufferIdx = (vk_activeBufferIdx + 1) % NUM_CMDBUFFERS;

	vk_frameStarted = false;
	return renderResult;
}

void QVk_BeginRenderpass(qvkrenderpasstype_t rpType)
{
	VkClearValue clearColors[3] = {
		{.color = { 1.f, .0f, .5f, 1.f } },
		{.depthStencil = { 1.f, 0 } },
		{.color = { 1.f, .0f, .5f, 1.f } },
	};

	VkRenderPassBeginInfo renderBeginInfo[] = {
		{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = vk_renderpasses[RP_WORLD].rp,
			.framebuffer = vk_framebuffers[RP_WORLD][vk_imageIndex],
			.renderArea.offset = { 0, 0 },
			.renderArea.extent = vk_swapchain.extent,
			.clearValueCount = vk_renderpasses[RP_WORLD].sampleCount != VK_SAMPLE_COUNT_1_BIT ? 3 : 2,
			.pClearValues = clearColors
		},
		{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = vk_renderpasses[RP_UI].rp,
			.framebuffer = vk_framebuffers[RP_UI][vk_imageIndex],
			.renderArea.offset = { 0, 0 },
			.renderArea.extent = vk_swapchain.extent,
			.clearValueCount = 2,
			.pClearValues = clearColors
		},
		{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = vk_renderpasses[RP_WORLD_WARP].rp,
			.framebuffer = vk_framebuffers[RP_WORLD_WARP][vk_imageIndex],
			.renderArea.offset = { 0, 0 },
			.renderArea.extent = vk_swapchain.extent,
			.clearValueCount = 1,
			.pClearValues = clearColors
		}
	};

	vkCmdBeginRenderPass(vk_commandbuffers[vk_activeBufferIdx], &renderBeginInfo[rpType], VK_SUBPASS_CONTENTS_INLINE);
}

void QVk_RecreateSwapchain()
{
	vkDeviceWaitIdle( vk_device.logical );
	DestroyFramebuffers();
	DestroyImageViews();
	VK_VERIFY( QVk_CreateSwapchain() );
	vk_viewport.width = (float)vid.width;
	vk_viewport.height = (float)vid.height;
	vk_scissor.extent = vk_swapchain.extent;
	DestroyDrawBuffers();
	CreateDrawBuffers();
	VK_VERIFY( CreateImageViews() );
	VK_VERIFY( CreateFramebuffers() );
}

uint8_t *QVk_GetVertexBuffer(VkDeviceSize size, VkBuffer *dstBuffer, VkDeviceSize *dstOffset)
{
	if (vk_dynVertexBuffers[vk_activeDynBufferIdx].currentOffset + size > vk_config.vertex_buffer_size)
	{
		vk_config.vertex_buffer_size = max(vk_config.vertex_buffer_size * BUFFER_RESIZE_FACTOR, NextPow2(size));

		ri.Con_Printf(PRINT_ALL, "Resizing dynamic vertex buffer to %ukB\n", vk_config.vertex_buffer_size / 1024);
		int swapBufferOffset = vk_swapBuffersCnt[vk_activeSwapBufferIdx];
		vk_swapBuffersCnt[vk_activeSwapBufferIdx] += NUM_DYNBUFFERS;

		if (vk_swapBuffers[vk_activeSwapBufferIdx] == NULL)
			vk_swapBuffers[vk_activeSwapBufferIdx] = malloc(sizeof(qvkbuffer_t) * vk_swapBuffersCnt[vk_activeSwapBufferIdx]);
		else
			vk_swapBuffers[vk_activeSwapBufferIdx] = realloc(vk_swapBuffers[vk_activeSwapBufferIdx], sizeof(qvkbuffer_t) * vk_swapBuffersCnt[vk_activeSwapBufferIdx]);

		for (int i = 0; i < NUM_DYNBUFFERS; ++i)
		{
			vk_swapBuffers[vk_activeSwapBufferIdx][swapBufferOffset + i] = vk_dynVertexBuffers[i];
			vmaUnmapMemory(vk_malloc, vk_dynVertexBuffers[i].allocation);

			QVk_CreateVertexBuffer(NULL, vk_config.vertex_buffer_size, &vk_dynVertexBuffers[i], NULL, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			vmaMapMemory(vk_malloc, vk_dynVertexBuffers[i].allocation, &vk_dynVertexBuffers[i].allocInfo.pMappedData);
		}
	}

	*dstOffset = vk_dynVertexBuffers[vk_activeDynBufferIdx].currentOffset;
	*dstBuffer = vk_dynVertexBuffers[vk_activeDynBufferIdx].buffer;
	vk_dynVertexBuffers[vk_activeDynBufferIdx].currentOffset += size;

	vk_config.vertex_buffer_usage = vk_dynVertexBuffers[vk_activeDynBufferIdx].currentOffset;
	if (vk_config.vertex_buffer_max_usage < vk_config.vertex_buffer_usage)
		vk_config.vertex_buffer_max_usage = vk_config.vertex_buffer_usage;
	
	return (uint8_t *)vk_dynVertexBuffers[vk_activeDynBufferIdx].allocInfo.pMappedData + (*dstOffset);
}

uint8_t *QVk_GetIndexBuffer(VkDeviceSize size, VkDeviceSize *dstOffset)
{
	// align to 4 bytes, so that we can reuse the buffer for both VK_INDEX_TYPE_UINT16 and VK_INDEX_TYPE_UINT32
	const int align_mod = size % 4;
	const uint32_t aligned_size = ((size % 4) == 0) ? size : (size + 4 - align_mod);

	if (vk_dynIndexBuffers[vk_activeDynBufferIdx].currentOffset + aligned_size > vk_config.index_buffer_size)
	{
		vk_config.index_buffer_size = max(vk_config.index_buffer_size * BUFFER_RESIZE_FACTOR, NextPow2(size));

		ri.Con_Printf(PRINT_ALL, "Resizing dynamic index buffer to %ukB\n", vk_config.index_buffer_size / 1024);
		int swapBufferOffset = vk_swapBuffersCnt[vk_activeSwapBufferIdx];
		vk_swapBuffersCnt[vk_activeSwapBufferIdx] += NUM_DYNBUFFERS;

		if (vk_swapBuffers[vk_activeSwapBufferIdx] == NULL)
			vk_swapBuffers[vk_activeSwapBufferIdx] = malloc(sizeof(qvkbuffer_t) * vk_swapBuffersCnt[vk_activeSwapBufferIdx]);
		else
			vk_swapBuffers[vk_activeSwapBufferIdx] = realloc(vk_swapBuffers[vk_activeSwapBufferIdx], sizeof(qvkbuffer_t) * vk_swapBuffersCnt[vk_activeSwapBufferIdx]);

		for (int i = 0; i < NUM_DYNBUFFERS; ++i)
		{
			vk_swapBuffers[vk_activeSwapBufferIdx][swapBufferOffset + i] = vk_dynIndexBuffers[i];
			vmaUnmapMemory(vk_malloc, vk_dynIndexBuffers[i].allocation);

			QVk_CreateIndexBuffer(NULL, vk_config.index_buffer_size, &vk_dynIndexBuffers[i], NULL, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			vmaMapMemory(vk_malloc, vk_dynIndexBuffers[i].allocation, &vk_dynIndexBuffers[i].allocInfo.pMappedData);
		}
	}

	*dstOffset = vk_dynIndexBuffers[vk_activeDynBufferIdx].currentOffset;
	vk_dynIndexBuffers[vk_activeDynBufferIdx].currentOffset += aligned_size;

	vk_config.index_buffer_usage = vk_dynIndexBuffers[vk_activeDynBufferIdx].currentOffset;
	if (vk_config.index_buffer_max_usage < vk_config.index_buffer_usage)
		vk_config.index_buffer_max_usage = vk_config.index_buffer_usage;

	return (uint8_t *)vk_dynIndexBuffers[vk_activeDynBufferIdx].allocInfo.pMappedData + (*dstOffset);
}

uint8_t *QVk_GetUniformBuffer(VkDeviceSize size, uint32_t *dstOffset, VkDescriptorSet *dstUboDescriptorSet)
{
	// 0x100 alignment is required by Vulkan spec
	const int align_mod = size % 256;
	const uint32_t aligned_size = ((size % 256) == 0) ? size : (size + 256 - align_mod);

	if (vk_dynUniformBuffers[vk_activeDynBufferIdx].currentOffset + UNIFORM_ALLOC_SIZE > vk_config.uniform_buffer_size)
	{
		vk_config.uniform_buffer_size = max(vk_config.uniform_buffer_size * BUFFER_RESIZE_FACTOR, NextPow2(size));

		ri.Con_Printf(PRINT_ALL, "Resizing dynamic uniform buffer to %ukB\n", vk_config.uniform_buffer_size / 1024);
		int swapBufferOffset   = vk_swapBuffersCnt[vk_activeSwapBufferIdx];
		int swapDescSetsOffset = vk_swapDescSetsCnt[vk_activeSwapBufferIdx];
		vk_swapBuffersCnt[vk_activeSwapBufferIdx]  += NUM_DYNBUFFERS;
		vk_swapDescSetsCnt[vk_activeSwapBufferIdx] += NUM_DYNBUFFERS;

		if (vk_swapBuffers[vk_activeSwapBufferIdx] == NULL)
			vk_swapBuffers[vk_activeSwapBufferIdx] = malloc(sizeof(qvkbuffer_t) * vk_swapBuffersCnt[vk_activeSwapBufferIdx]);
		else
			vk_swapBuffers[vk_activeSwapBufferIdx] = realloc(vk_swapBuffers[vk_activeSwapBufferIdx], sizeof(qvkbuffer_t) * vk_swapBuffersCnt[vk_activeSwapBufferIdx]);

		if (vk_swapDescriptorSets[vk_activeSwapBufferIdx] == NULL)
			vk_swapDescriptorSets[vk_activeSwapBufferIdx] = malloc(sizeof(VkDescriptorSet) * vk_swapDescSetsCnt[vk_activeSwapBufferIdx]);
		else
			vk_swapDescriptorSets[vk_activeSwapBufferIdx] = realloc(vk_swapDescriptorSets[vk_activeSwapBufferIdx], sizeof(VkDescriptorSet) * vk_swapDescSetsCnt[vk_activeSwapBufferIdx]);

		for (int i = 0; i < NUM_DYNBUFFERS; ++i)
		{
			vk_swapBuffers[vk_activeSwapBufferIdx][swapBufferOffset + i] = vk_dynUniformBuffers[i];
			vk_swapDescriptorSets[vk_activeSwapBufferIdx][swapDescSetsOffset + i] = vk_uboDescriptorSets[i];;
			vmaUnmapMemory(vk_malloc, vk_dynUniformBuffers[i].allocation);

			QVk_CreateUniformBuffer(vk_config.uniform_buffer_size, &vk_dynUniformBuffers[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			vmaMapMemory(vk_malloc, vk_dynUniformBuffers[i].allocation, &vk_dynUniformBuffers[i].allocInfo.pMappedData);
			CreateUboDescriptorSet(&vk_uboDescriptorSets[i], vk_dynUniformBuffers[i].buffer);
		}
	}

	*dstOffset = vk_dynUniformBuffers[vk_activeDynBufferIdx].currentOffset;
	*dstUboDescriptorSet = vk_uboDescriptorSets[vk_activeDynBufferIdx];
	vk_dynUniformBuffers[vk_activeDynBufferIdx].currentOffset += aligned_size;

	vk_config.uniform_buffer_usage = vk_dynUniformBuffers[vk_activeDynBufferIdx].currentOffset;
	if (vk_config.uniform_buffer_max_usage < vk_config.uniform_buffer_usage)
		vk_config.uniform_buffer_max_usage = vk_config.uniform_buffer_usage;

	return (uint8_t *)vk_dynUniformBuffers[vk_activeDynBufferIdx].allocInfo.pMappedData + (*dstOffset);
}

uint8_t *QVk_GetStagingBuffer(VkDeviceSize size, int alignment, VkCommandBuffer *cmdBuffer, VkBuffer *buffer, uint32_t *dstOffset)
{
	qvkstagingbuffer_t * stagingBuffer = &vk_stagingBuffers[vk_activeStagingBuffer];
	const int align_mod = stagingBuffer->buffer.currentOffset % alignment;
	stagingBuffer->buffer.currentOffset = ((stagingBuffer->buffer.currentOffset % alignment) == 0)
		? stagingBuffer->buffer.currentOffset : (stagingBuffer->buffer.currentOffset + alignment - align_mod);

	if (size > STAGING_BUFFER_MAXSIZE)
		Sys_Error("Cannot allocate staging buffer space");

	if ((stagingBuffer->buffer.currentOffset + size) >= STAGING_BUFFER_MAXSIZE && !stagingBuffer->submitted)
		SubmitStagingBuffer(vk_activeStagingBuffer);

	stagingBuffer = &vk_stagingBuffers[vk_activeStagingBuffer];
	if (stagingBuffer->submitted)
	{
		VK_VERIFY(vkWaitForFences(vk_device.logical, 1, &stagingBuffer->fence, VK_TRUE, UINT64_MAX));
		VK_VERIFY(vkResetFences(vk_device.logical, 1, &stagingBuffer->fence));

		stagingBuffer->buffer.currentOffset = 0;
		stagingBuffer->submitted = false;

		VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.pNext = NULL,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			.pInheritanceInfo = NULL
		};

		VK_VERIFY(vkBeginCommandBuffer(stagingBuffer->cmdBuffer, &beginInfo));
	}

	if (cmdBuffer)
		*cmdBuffer = stagingBuffer->cmdBuffer;
	if (buffer)
		*buffer = stagingBuffer->buffer.buffer;
	if (dstOffset)
		*dstOffset = stagingBuffer->buffer.currentOffset;

	unsigned char *data = (uint8_t *)stagingBuffer->buffer.allocInfo.pMappedData + stagingBuffer->buffer.currentOffset;
	stagingBuffer->buffer.currentOffset += size;

	return data;
}

VkBuffer QVk_GetTriangleFanIbo(VkDeviceSize indexCount)
{
	if (indexCount > vk_config.triangle_fan_index_usage)
		vk_config.triangle_fan_index_usage = indexCount;

	if (vk_config.triangle_fan_index_usage > vk_config.triangle_fan_index_max_usage)
		vk_config.triangle_fan_index_max_usage = vk_config.triangle_fan_index_usage;

	if (indexCount > vk_config.triangle_fan_index_count)
	{
		vk_config.triangle_fan_index_count *= BUFFER_RESIZE_FACTOR;
		ri.Con_Printf(PRINT_ALL, "Resizing triangle fan index buffer to %u indices.\n", vk_config.triangle_fan_index_count);
		RebuildTriangleFanIndexBuffer();
	}

	return *vk_triangleFanIbo;
}

void QVk_SubmitStagingBuffers()
{
	for (int i = 0; i < NUM_DYNBUFFERS; ++i)
	{
		if (!vk_stagingBuffers[i].submitted && vk_stagingBuffers[i].buffer.currentOffset > 0)
			SubmitStagingBuffer(i);
	}
}

VkSampler QVk_UpdateTextureSampler(qvktexture_t *texture, qvksampler_t samplerType)
{
	assert((vk_samplers[samplerType] != VK_NULL_HANDLE) && "Sampler is VK_NULL_HANDLE!");

	VkDescriptorImageInfo dImgInfo = {
		.sampler = vk_samplers[samplerType],
		.imageView = texture->imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};

	VkWriteDescriptorSet writeSet = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = NULL,
		.dstSet = texture->descriptorSet,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &dImgInfo,
		.pBufferInfo = NULL,
		.pTexelBufferView = NULL
	};

	vkUpdateDescriptorSets(vk_device.logical, 1, &writeSet, 0, NULL);

	return vk_samplers[samplerType];
}

void QVk_DrawColorRect(float *ubo, VkDeviceSize uboSize, qvkrenderpasstype_t rpType)
{
	uint32_t uboOffset;
	VkDescriptorSet uboDescriptorSet;
	uint8_t *vertData = QVk_GetUniformBuffer(uboSize, &uboOffset, &uboDescriptorSet);
	memcpy(vertData, ubo, uboSize);

	QVk_BindPipeline(&vk_drawColorQuadPipeline[rpType]);
	VkDeviceSize offsets = 0;
	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_drawColorQuadPipeline[rpType].layout, 0, 1, &uboDescriptorSet, 1, &uboOffset);
	vkCmdBindVertexBuffers(vk_activeCmdbuffer, 0, 1, &vk_colorRectVbo.buffer, &offsets);
	vkCmdBindIndexBuffer(vk_activeCmdbuffer, vk_rectIbo.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(vk_activeCmdbuffer, 6, 1, 0, 0, 0);
}

void QVk_DrawTexRect(float *ubo, VkDeviceSize uboSize, qvktexture_t *texture)
{
	uint32_t uboOffset;
	VkDescriptorSet uboDescriptorSet;
	uint8_t *uboData = QVk_GetUniformBuffer(uboSize, &uboOffset, &uboDescriptorSet);
	memcpy(uboData, ubo, uboSize);

	QVk_BindPipeline(&vk_drawTexQuadPipeline);
	VkDeviceSize offsets = 0;
	VkDescriptorSet descriptorSets[] = { texture->descriptorSet, uboDescriptorSet };
	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_drawTexQuadPipeline.layout, 0, 2, descriptorSets, 1, &uboOffset);
	vkCmdBindVertexBuffers(vk_activeCmdbuffer, 0, 1, &vk_texRectVbo.buffer, &offsets);
	vkCmdBindIndexBuffer(vk_activeCmdbuffer, vk_rectIbo.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(vk_activeCmdbuffer, 6, 1, 0, 0, 0);
}

void QVk_BindPipeline(qvkpipeline_t *pipeline)
{
	if (vk_state.current_pipeline != pipeline->pl)
	{
		vkCmdBindPipeline(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pl);
		vk_state.current_pipeline = pipeline->pl;
	}
}

const char *QVk_GetError(VkResult errorCode)
{
#define ERRSTR(r) case VK_ ##r: return "VK_"#r
	switch (errorCode)
	{
		ERRSTR(NOT_READY);
		ERRSTR(TIMEOUT);
		ERRSTR(EVENT_SET);
		ERRSTR(EVENT_RESET);
		ERRSTR(INCOMPLETE);
		ERRSTR(ERROR_OUT_OF_HOST_MEMORY);
		ERRSTR(ERROR_OUT_OF_DEVICE_MEMORY);
		ERRSTR(ERROR_INITIALIZATION_FAILED);
		ERRSTR(ERROR_DEVICE_LOST);
		ERRSTR(ERROR_MEMORY_MAP_FAILED);
		ERRSTR(ERROR_LAYER_NOT_PRESENT);
		ERRSTR(ERROR_EXTENSION_NOT_PRESENT);
		ERRSTR(ERROR_FEATURE_NOT_PRESENT);
		ERRSTR(ERROR_INCOMPATIBLE_DRIVER);
		ERRSTR(ERROR_TOO_MANY_OBJECTS);
		ERRSTR(ERROR_FORMAT_NOT_SUPPORTED);
		ERRSTR(ERROR_SURFACE_LOST_KHR);
		ERRSTR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
		ERRSTR(SUBOPTIMAL_KHR);
		ERRSTR(ERROR_OUT_OF_DATE_KHR);
		ERRSTR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
		ERRSTR(ERROR_VALIDATION_FAILED_EXT);
		ERRSTR(ERROR_INVALID_SHADER_NV);
		default: return "<unknown>";
	}
#undef ERRSTR
	return "UNKNOWN ERROR";
}

void Vkimp_EnableLogging(qboolean enable)
{
	if (enable)
	{
		if (!vkw_state.log_fp)
		{
			struct tm *newtime;
			time_t aclock;
			char buffer[1024];

			time(&aclock);
			newtime = localtime(&aclock);

			asctime(newtime);

			Com_sprintf(buffer, sizeof(buffer), "%s/vk.log", ri.FS_Gamedir());
			vkw_state.log_fp = fopen(buffer, "wt");

			fprintf(vkw_state.log_fp, "%s\n", asctime(newtime));
		}
		vk_logfp = vkw_state.log_fp;
	}
	else
	{
		vk_logfp = NULL;
	}
}

void Vkimp_LogNewFrame( void )
{
	fprintf( vkw_state.log_fp, "*** R_BeginFrame ***\n" );
}
