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
    uint32_t occupant;
};

enum class EntityType
{
    Leader, Follower, Hostile,
};

struct Entity
{
    uint32_t x, y;
    EntityType type;
};

struct Map
{
    std::vector<std::vector<Cell>> cells;
};

enum class InputEvent
{
    Up, Left, Down, Right,
};

struct Enemy
{
    uint32_t entity;
    std::vector<std::pair<uint32_t, uint32_t>> trackPoints;
    uint32_t currentTrackPoint;
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
    std::vector<uint32_t> playerEntities;
    std::vector<Enemy> enemies;

    double tickTimer = 0;
    static constexpr double tickInterval = 0.5;
    std::deque<InputEvent> inputQueue;

    void initPlayer(const std::initializer_list<std::pair<uint32_t, uint32_t>>& positions)
    {
        playerEntities.reserve(positions.size());
        EntityType type = EntityType::Leader;
        for (const auto& [x, y] : positions)
        {
            map.cells[y][x].occupant = entities.size();
            playerEntities.push_back(entities.size());
            entities.push_back(Entity{ .x = x, .y = y, .type = type });
            type = EntityType::Follower;
        }
    }

    void createEnemy(uint32_t x, uint32_t y, const std::vector<std::pair<uint32_t, uint32_t>>& trackPoints)
    {
        map.cells[y][x].occupant = entities.size();
        enemies.push_back(Enemy{
                    .entity = static_cast<uint32_t>(entities.size()),
                    .trackPoints = trackPoints,
                    .currentTrackPoint = 0,
                });
        entities.push_back(Entity{ .x = x, .y = y, .type = EntityType::Hostile });
    }

    void init(eng::ResourceLoaderInterface& resourceLoader, eng::SceneInterface& scene, eng::InputInterface& input) override
    {
        textures.blank = resourceLoader.loadTexture("textures/blank.png");

        using namespace std::string_view_literals;
        constexpr std::array mapRows {
            "XXXXXXXXXXXXXXXXXXXX"sv,
            "X__________________X"sv,
            "X________XE________X"sv,
            "X__________________X"sv,
            "X___XE_____________X"sv,
            "X______________XE__X"sv,
            "X__________________X"sv,
            "X________P_________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "XXXXXXXXXXXXXXXXXXXX"sv,
        };

        map.cells.resize(mapRows.size());
        std::map<char, std::vector<std::pair<uint32_t, uint32_t>>> markers;
        for (uint32_t row = 0; row < mapRows.size(); ++row)
        {
            map.cells[row].resize(mapRows[row].size());
            for (uint32_t col = 0; col < mapRows[row].size(); ++col)
            {
                map.cells[row][col] = Cell {};
                switch (mapRows[row][col])
                {
                    case 'X':
                        map.cells[row][col].solid = true;
                        break;
                    case '_':
                        break;
                    default:
                        markers[mapRows[row][col]].push_back({ col, row });
                        break;
                }
            }
        }

        if (auto it = markers.find('P'); it != markers.end() && !it->second.empty())
        {
            auto&& [x, y] = it->second.front();
            initPlayer({ {x, y}, {x-1, y}, {x-1, y-1} });
        }
        if (auto it = markers.find('E'); it != markers.end())
        {
            for (auto&& [x, y] : it->second)
            {
                createEnemy(x, y, { {x, y+1}, {x-2, y+1}, {x-2, y-1}, {x, y-1} });
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

            if (!playerEntities.empty())
            {
                Entity& player = entities[playerEntities.front()];
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
                    for (uint32_t i = playerEntities.size() - 1; i > 0; --i)
                    {
                        entities[playerEntities[i]].x = entities[playerEntities[i - 1]].x;
                        entities[playerEntities[i]].y = entities[playerEntities[i - 1]].y;
                    }

                    player.x += dx;
                    player.y += dy;
                }
            }
        }

        for (auto& enemy : enemies)
        {
            auto& entity = entities[enemy.entity];
            auto [px, py] = enemy.trackPoints[enemy.currentTrackPoint];
            if (entity.x == px && entity.y == py)
            {
                enemy.currentTrackPoint = (enemy.currentTrackPoint + 1) % enemy.trackPoints.size();
                std::tie(px, py) = enemy.trackPoints[enemy.currentTrackPoint];
            }

            int dx = (int)px - (int)entity.x, dy = (int)py - (int)entity.y;
            if (std::abs(dx) > std::abs(dy))
            {
                dy = 0;
                dx = glm::clamp(dx, -1, 1);
            }
            else
            {
                dx = 0;
                dy = glm::clamp(dy, -1, 1);
            }

            entity.x += dx;
            entity.y += dy;
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

        for (const auto& entity : entities)
        {
            glm::vec4 color;
            uint32_t textureIndex = textures.blank;
            switch (entity.type)
            {
                case EntityType::Leader:
                    color = glm::vec4(1, 1, 0, 1);
                    break;
                case EntityType::Follower:
                    color = glm::vec4(0, 1, 1, 1);
                    break;
                case EntityType::Hostile:
                    color = glm::vec4(1, 0, 0, 1);
                    break;
            }

            scene.instances().push_back(eng::Instance {
                        .position = glm::vec2(1 + 2 * entity.x, 2 * (map.cells.size() - entity.y) - 1),
                        .scale = glm::vec2(1, 1),
                        .textureIndex = textureIndex,
                        .tintColor = color,
                    });
        }

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
