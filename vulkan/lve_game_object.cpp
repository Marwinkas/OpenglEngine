#include "lve_game_object.hpp"

#include <numeric>

namespace burnhope {
BurnhopeGameObject& BurnhopeGameObjectManager::makePointLight(
    float intensity, float radius, glm::vec3 color) {
  auto& gameObj = createGameObject();
  gameObj.color = color;
  gameObj.pointLight = std::make_unique<PointLightComponent>();
  gameObj.pointLight->lightIntensity = intensity;
  return gameObj;
}

BurnhopeGameObjectManager::BurnhopeGameObjectManager(BurnhopeDevice& device) {
  // including nonCoherentAtomSize allows us to flush a specific index at once
  int alignment = std::lcm(
      device.properties.limits.nonCoherentAtomSize,
      device.properties.limits.minUniformBufferOffsetAlignment);
  for (int i = 0; i < uboBuffers.size(); i++) {
    uboBuffers[i] = std::make_unique<BurnhopeBuffer>(
        device,
        sizeof(GameObjectBufferData),
        BurnhopeGameObjectManager::MAX_GAME_OBJECTS,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        alignment);
    uboBuffers[i]->map();
  }

  textureDefault = BurnhopeTexture::createTextureFromFile(device, "../textures/missing.png");
}

void BurnhopeGameObjectManager::updateBuffer(int frameIndex) {
  // copy model matrix and normal matrix for each gameObj into
  // buffer for this frame
  for (auto& kv : gameObjects) {
    auto& obj = kv.second;
    GameObjectBufferData data{};
    uboBuffers[frameIndex]->writeToIndex(&data, kv.first);
  }
  uboBuffers[frameIndex]->flush();
}

VkDescriptorBufferInfo BurnhopeGameObject::getBufferInfo(int frameIndex) {
  return gameObjectManager.getBufferInfoForGameObject(frameIndex, id);
}

BurnhopeGameObject::BurnhopeGameObject(id_t objId, const BurnhopeGameObjectManager& manager)
    : id{objId}, gameObjectManager{manager} {}

}  // namespace burnhope