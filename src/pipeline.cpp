#include "pipeline.hpp"

#include <fmt/core.h>

#include <fstream>

std::vector<char> Pipeline::readFile(const std::string& filePath) {
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);
	if (!file.is_open()) throw std::runtime_error("failed to open file: " + filePath);
	size_t fileSize = file.tellg();
	std::vector<char> buf(fileSize);
	file.seekg(0);
	file.read(buf.data(), fileSize);
	file.close();
	return buf;
}

void Pipeline::createGraphicsPipeline(const std::string& vertFilePath, const std::string& fragFilePath, const PipelineConfigInfo& configInfo) {
	auto vertCode = readFile(vertFilePath);
	auto fragCode = readFile(fragFilePath);

	fmt::print("vertex shader code size: {}\n", vertCode.size());
	fmt::print("fragment shader code size: {}\n", fragCode.size());
}

Pipeline::Pipeline(Device& device, const std::string& vertFilePath, const std::string& fragFilePath, const PipelineConfigInfo& configInfo) : device(device) {
	createGraphicsPipeline(vertFilePath, fragFilePath, configInfo);
}

void Pipeline::createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule) {
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	if (vkCreateShaderModule(device.device(), &createInfo, nullptr, shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("failed to create shader module");
	}
}

PipelineConfigInfo Pipeline::defaultPipelineConfigInfo(uint32_t w, uint32_t h) {
	PipelineConfigInfo configInfo{};
	return configInfo;
}