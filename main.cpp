#include "engine.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <deque>
#include <map>
#include <string_view>
#include <iostream>

struct Cell
{
    bool solid = false;
};

struct Entity
{
};

struct Map
{
    std::vector<std::vector<Cell>> cells;
};

enum class InputEvent
{
    Up, Left, Down, Right,
};

struct GameLogic final : eng::GameLogicInterface
{
    struct
    {
        uint32_t blank;
    } textures;

    std::map<InputEvent, uint32_t> inputMappings;

    Map map;
    std::vector<Entity> entities;
    std::deque<InputEvent> inputQueue;

    double tickTimer = 0;
    static constexpr double tickInterval = 0.5;

    struct {
        uint32_t x = 5, y = 5;
    } player;

    void init(eng::ResourceLoaderInterface& resourceLoader, eng::SceneInterface& scene, eng::InputInterface& input) override
    {
        textures.blank = resourceLoader.loadTexture("textures/blank.png");

        using namespace std::string_view_literals;
        constexpr std::array mapRows {
            "XXXXXXXXXXXXXXXXXXXX"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "XXXXXXXXXXXXXXXXXXXX"sv,
        };

        map.cells.reserve(mapRows.size());
        for (const auto& row : mapRows)
        {
            auto& cellsRow = map.cells.emplace_back(row.size());
            for (uint32_t i = 0; i < row.size(); ++i)
            {
                cellsRow[i].solid = row[i] == 'X';
            }
        }

        inputMappings[InputEvent::Up] = input.createMapping();
        inputMappings[InputEvent::Left] = input.createMapping();
        inputMappings[InputEvent::Down] = input.createMapping();
        inputMappings[InputEvent::Right] = input.createMapping();

        input.mapKey(inputMappings[InputEvent::Up], glfwGetKeyScancode(GLFW_KEY_W));
        input.mapKey(inputMappings[InputEvent::Left], glfwGetKeyScancode(GLFW_KEY_A));
        input.mapKey(inputMappings[InputEvent::Down], glfwGetKeyScancode(GLFW_KEY_S));
        input.mapKey(inputMappings[InputEvent::Right], glfwGetKeyScancode(GLFW_KEY_D));

        scene.projection() = glm::orthoLH_ZO(0.0f, 40.0f, 24.0f, 0.0f, 0.0f, 1.0f);
    }

    void gameTick()
    {
        if (!inputQueue.empty())
        {
            InputEvent event = inputQueue.front();
            inputQueue.pop_front();

            int dx = 0, dy = 0;
            switch (event)
            {
                case InputEvent::Up:
                    dy = -1;
                    break;
                case InputEvent::Left:
                    dx = -1;
                    break;
                case InputEvent::Down:
                    dy = 1;
                    break;
                case InputEvent::Right:
                    dx = 1;
                    break;
            }

            if (dx < 0 && player.x == 0)
            {
                dx = 0;
            }
            if (dx > 0 && player.x == map.cells[0].size() - 1)
            {
                dx = 0;
            }
            if (dy < 0 && player.y == 0)
            {
                dy = 0;
            }
            if (dy > 0 && player.y == map.cells.size() - 1)
            {
                dy = 0;
            }

            if (!map.cells[player.y + dy][player.x + dx].solid)
            {
                player.x += dx;
                player.y += dy;
            }
        }
    }

    void runFrame(eng::SceneInterface& scene, eng::InputInterface& input, const double deltaTime) override
    {
        for (auto&& [ event, mapping ] : inputMappings)
        {
            if (input.getBoolean(mapping, eng::InputInterface::BoolStateEvent::Pressed))
            {
                inputQueue.push_back(event);
            }
        }

        if (tickTimer >= tickInterval)
        {
            gameTick();
            tickTimer -= tickInterval;
        }
        tickTimer += deltaTime;

        scene.instances().clear();
        for (uint32_t i = 0; i < map.cells.size(); ++i)
        {
            for (uint32_t j = 0; j < map.cells[i].size(); ++j)
            {
                if (map.cells[i][j].solid)
                {
                    scene.instances().push_back(eng::Instance {
                                .position = glm::vec2(1 + 2 * j, 2 * (map.cells.size() - i) - 1),
                                .scale = glm::vec2(1, 1),
                                .textureIndex = textures.blank,
                                .tintColor = glm::vec4(1, 1, 1, 1),
                            });
                }
            }
        }

        scene.instances().push_back(eng::Instance {
                    .position = glm::vec2(1 + 2 * player.x, 2 * (map.cells.size() - player.y) - 1),
                    .scale = glm::vec2(1, 1),
                    .textureIndex = textures.blank,
                    .tintColor = glm::vec4(0, 1, 1, 1),
                });
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
