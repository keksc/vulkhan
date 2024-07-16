#pragma once

#include "device.hpp"

#include <string>
#include <vector>

struct PipelineConfigInfo {};

class Pipeline {
	static std::vector<char> readFile(const std::string& filePath);
	void createGraphicsPipeline(const std::string& vertFilePath, const std::string& fragFilePath, const PipelineConfigInfo& configInfo);
	void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);
	Device& device;
	VkPipeline graphicsPipeline;
	VkShaderModule vertShaderModule, fragShaderModule;
public:
	Pipeline(Device& device, const std::string& vertFilePath, const std::string& fragFilePath, const PipelineConfigInfo& configInfo);
	~Pipeline() {}
	static PipelineConfigInfo defaultPipelineConfigInfo(uint32_t w, uint32_t h);
};