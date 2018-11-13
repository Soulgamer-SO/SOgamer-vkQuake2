/*
Copyright (C) 2018 Krzysztof Kondrak

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

#include "vk_local.h"

qvkshader_t QVk_CreateShader(const uint32_t *shaderSrc, size_t shaderCodeSize, VkShaderStageFlagBits shaderStage)
{
	qvkshader_t shader;
	VkShaderModuleCreateInfo smCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.codeSize = shaderCodeSize,
		.pCode = shaderSrc
	};

	VK_VERIFY(vkCreateShaderModule(vk_device.logical, &smCreateInfo, NULL, &shader.module));

	VkPipelineShaderStageCreateInfo vssCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.stage = shaderStage,
		.module = shader.module,
		.pName = "main",
		.pSpecializationInfo = NULL
	};

	shader.createInfo = vssCreateInfo;

	return shader;
}


void QVk_CreatePipeline(const VkDescriptorSetLayout *descriptorLayout, const uint32_t descLayoutCount, const VkPipelineVertexInputStateCreateInfo *vertexInputInfo,
						qvkpipeline_t *pipeline, const qvkshader_t *shaders, uint32_t shaderCount)
{
	VkPipelineShaderStageCreateInfo *ssCreateInfos = (VkPipelineShaderStageCreateInfo *)malloc(shaderCount * sizeof(VkPipelineShaderStageCreateInfo));
	for (int i = 0; i < shaderCount; i++)
	{
		ssCreateInfos[i] = shaders[i].createInfo;
	}

	VkPipelineInputAssemblyStateCreateInfo iaCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.topology = pipeline->topology,
		.primitiveRestartEnable = VK_FALSE
	};

	VkViewport viewport = {
		.x = 0.f,
		.y = 0.f,
		.width = (float)vk_swapchain.extent.width,
		.height = (float)vk_swapchain.extent.height,
		.minDepth = 0.f,
		.maxDepth = 1.f,
	};

	VkRect2D scissor = {
		.offset.x = 0,
		.offset.y = 0,
		.extent = vk_swapchain.extent
	};

	VkPipelineViewportStateCreateInfo vpCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor
	};

	VkPipelineRasterizationStateCreateInfo rCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = pipeline->mode,
		.cullMode = pipeline->cullMode,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.depthBiasConstantFactor = 0.f,
		.depthBiasClamp = 0.f,
		.depthBiasSlopeFactor = 0.f,
		.lineWidth = 1.f
	};

	VkPipelineMultisampleStateCreateInfo msCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.rasterizationSamples = vk_activeRenderpass.sampleCount,
		.sampleShadingEnable = pipeline->minSampleShading < 0.f ? VK_FALSE : VK_TRUE,
		.minSampleShading = pipeline->minSampleShading < 0.f ? 1.f : pipeline->minSampleShading,
		.pSampleMask = NULL,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE
	};

	VkPipelineDepthStencilStateCreateInfo dCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.depthTestEnable = pipeline->depthTestEnable,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
		.front = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER, 0, 0, 0 },
		.back = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER, 0, 0, 0 },
		.minDepthBounds = 0.f,
		.maxDepthBounds = 1.f
	};

	VkPipelineColorBlendStateCreateInfo cbsCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments = &pipeline->blendOpts,
		.blendConstants[0] = 0.f,
		.blendConstants[1] = 0.f,
		.blendConstants[2] = 0.f,
		.blendConstants[3] = 0.f
	};

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dsCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamicStates
	};

	VkPipelineLayoutCreateInfo plCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.setLayoutCount = descLayoutCount,
		.pSetLayouts = descriptorLayout,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges = NULL
	};

	VK_VERIFY(vkCreatePipelineLayout(vk_device.logical, &plCreateInfo, NULL, &pipeline->layout));

	// create THE pipeline
	VkGraphicsPipelineCreateInfo pCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = NULL,
		.flags = pipeline->flags,
		.stageCount = shaderCount,
		.pStages = ssCreateInfos,
		.pVertexInputState = vertexInputInfo,
		.pInputAssemblyState = &iaCreateInfo,
		.pTessellationState = NULL,
		.pViewportState = &vpCreateInfo,
		.pRasterizationState = &rCreateInfo,
		.pMultisampleState = &msCreateInfo,
		.pDepthStencilState = &dCreateInfo,
		.pColorBlendState = &cbsCreateInfo,
		.pDynamicState = &dsCreateInfo,
		.layout = pipeline->layout,
		.renderPass = vk_activeRenderpass.rp,
		.subpass = 0,
		.basePipelineHandle = pipeline->basePipelineHandle,
		.basePipelineIndex = -1
	};

	VK_VERIFY(vkCreateGraphicsPipelines(vk_device.logical, pipeline->cache, 1, &pCreateInfo, NULL, &pipeline->pl));
	free(ssCreateInfos);
}

void QVk_DestroyPipeline(qvkpipeline_t *pipeline)
{
	if (pipeline->layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(vk_device.logical, pipeline->layout, NULL);
	if (pipeline->pl != VK_NULL_HANDLE)
		vkDestroyPipeline(vk_device.logical, pipeline->pl, NULL);

	pipeline->layout = VK_NULL_HANDLE;
	pipeline->pl = VK_NULL_HANDLE;
}