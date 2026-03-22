#pragma once

#include "Device.hpp"

// std
#include <string>
#include <vector>

namespace burnhope {

struct PipelineConfigInfo {
  PipelineConfigInfo() = default;
  PipelineConfigInfo(const PipelineConfigInfo&) = delete;
  PipelineConfigInfo& operator=(const PipelineConfigInfo&) = delete;

  std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
  VkPipelineViewportStateCreateInfo viewportInfo;
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
  VkPipelineRasterizationStateCreateInfo rasterizationInfo;
  VkPipelineMultisampleStateCreateInfo multisampleInfo;
  VkPipelineColorBlendAttachmentState colorBlendAttachment;
  VkPipelineColorBlendStateCreateInfo colorBlendInfo;
  VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
  std::vector<VkDynamicState> dynamicStateEnables;
  VkPipelineDynamicStateCreateInfo dynamicStateInfo;
  VkPipelineLayout pipelineLayout = nullptr;
  VkRenderPass renderPass = nullptr;
  uint32_t subpass = 0;
};

class BurnhopePipeline {
 public:
  BurnhopePipeline(
      BurnhopeDevice& device,
      const std::string& vertFilepath,
      const std::string& fragFilepath,
      const PipelineConfigInfo& configInfo);
  ~BurnhopePipeline();


  // В секцию public:
  BurnhopePipeline(
      BurnhopeDevice& device,
      const std::string& compFilepath,
      VkPipelineLayout pipelineLayout); // Новый конструктор для Compute

  void bindCompute(VkCommandBuffer commandBuffer); // Новый метод бинда

  // В секцию private:
  void createComputePipeline(const std::string& compFilepath, VkPipelineLayout pipelineLayout);

  VkShaderModule compShaderModule = VK_NULL_HANDLE; // Модуль шейдера
  VkPipeline computePipeline = VK_NULL_HANDLE;      // Сам конвейер






  BurnhopePipeline(const BurnhopePipeline&) = delete;
  BurnhopePipeline& operator=(const BurnhopePipeline&) = delete;

  void bind(VkCommandBuffer commandBuffer);

  static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);
  static void enableAlphaBlending(PipelineConfigInfo& configInfo);

 private:
  static std::vector<char> readFile(const std::string& filepath);

  void createGraphicsPipeline(
      const std::string& vertFilepath,
      const std::string& fragFilepath,
      const PipelineConfigInfo& configInfo);

  void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);

  BurnhopeDevice& lveDevice;
  VkPipeline graphicsPipeline;
  VkShaderModule vertShaderModule;
  VkShaderModule fragShaderModule;
};
}  // namespace burnhope
