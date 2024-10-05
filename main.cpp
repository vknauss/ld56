#include "engine.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <string_view>
#include <iostream>

struct GameLogic final : eng::GameLogicInterface
{
    struct
    {
        uint32_t blank;
    } textures;

    struct
    {
        uint32_t up;
        uint32_t left;
        uint32_t down;
        uint32_t right;
    } inputs;


    void init(eng::ResourceLoaderInterface& resourceLoader, eng::SceneInterface& scene, eng::InputInterface& input) override
    {
        textures.blank = resourceLoader.loadTexture("textures/blank.png");

        using namespace std::string_view_literals;
        constexpr std::array mapRows {
            "X__________________X"sv,
            "X_X_X__X__X_X__X___X"sv,
            "X_X_X_X_X_X_X_X_X__X"sv,
            "X_XXX_XXX_XXX_XXX__X"sv,
            "X_X_X_X_X_X_X_X_X__X"sv,
            "X__________________X"sv,
            "X_X_X_XXX__X__X_X__X"sv,
            "X_X_X_X___X_X_X_X__X"sv,
            "X__X__XX__XXX_XXX__X"sv,
            "X__X__XXX_X_X_X_X__X"sv,
            "X__________________X"sv,
            "XXXXXXXXXXXXXXXXXXXX"sv,
        };

        inputs.up = input.createMapping();
        inputs.left = input.createMapping();
        inputs.down = input.createMapping();
        inputs.right = input.createMapping();

        input.mapKey(inputs.up, glfwGetKeyScancode(GLFW_KEY_W));
        input.mapKey(inputs.left, glfwGetKeyScancode(GLFW_KEY_A));
        input.mapKey(inputs.down, glfwGetKeyScancode(GLFW_KEY_S));
        input.mapKey(inputs.right, glfwGetKeyScancode(GLFW_KEY_D));

        scene.projection() = glm::orthoLH_ZO(0.0f, 40.0f, 24.0f, 0.0f, 0.0f, 1.0f);
    }

    void runFrame(eng::SceneInterface& scene, eng::InputInterface& input, const double deltaTime) override
    {
        scene.instances().clear();
    }

    void cleanup() override
    {
    }
};

int main(int argc, const char** argv)
{
    GameLogic gameLogic;
    eng::run(gameLogic, eng::ApplicationInfo {
            .appName = "gubgub",
            .appVersion = 0,
            .windowTitle = "gubgub",
            .windowWidth = 1280,
            .windowHeight = 768,
        });
}
