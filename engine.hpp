#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace eng
{
    struct Instance
    {
        glm::vec2 position = { 0, 0 };
        glm::vec2 scale = { 1, 1 };
        float angle = 0.0f;
        uint32_t textureIndex = 0;
        glm::vec4 tintColor = { 1, 1, 1, 1 };
    };

    struct ResourceLoaderInterface
    {
        virtual uint32_t loadTexture(const std::string& filePath) = 0;
    };

    struct SceneInterface
    {
        virtual std::vector<Instance>& instances() = 0;
        virtual glm::mat4& projection() = 0;
    };

    struct InputInterface
    {
        enum class CursorAxis
        {
            X,
            Y,
        };

        enum class BoolStateEvent
        {
            Down,
            Pressed,
            Released,
        };

        enum class RealStateEvent
        {
            Value,
            Delta,
        };

        virtual uint32_t createMapping() = 0;

        virtual void mapKey(const uint32_t mapping, const int scancode) = 0;
        virtual void mapMouseButton(const uint32_t mapping, const int button) = 0;
        virtual void mapCursor(const uint32_t mapping, const CursorAxis axis) = 0;

        virtual bool getBoolean(const uint32_t mapping, const BoolStateEvent event) const = 0;
        virtual double getReal(const uint32_t mapping, const RealStateEvent event) const = 0;
    };

    struct GameLogicInterface
    {
        virtual ~GameLogicInterface() = default;

        virtual void init(ResourceLoaderInterface& resourceLoader, SceneInterface& scene, InputInterface& input) = 0;
        virtual void runFrame(SceneInterface& scene, InputInterface& input, const double deltaTime) = 0;
        virtual void cleanup() = 0;
    };

    struct ApplicationInfo
    {
        std::string appName;
        uint32_t appVersion;
        std::string windowTitle;
        uint32_t windowWidth;
        uint32_t windowHeight;
    };

    void run(GameLogicInterface& gameLogic, const ApplicationInfo& applicationInfo);
}
