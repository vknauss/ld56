#include "engine.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <string_view>
#include <typeinfo>
#include <iostream>

enum class EntityType
{
    Leader, Follower, Hostile,
};

struct ComponentArrayBase
{
    virtual ~ComponentArrayBase() = default;

    virtual bool has(uint32_t) = 0;
    virtual void remove(uint32_t) = 0;
};

template<typename ComponentType>
struct ComponentArray : ComponentArrayBase
{
    std::vector<ComponentType> components;
    std::vector<uint32_t> entities;
    std::vector<uint32_t> indices;

    bool has(uint32_t entity) override
    {
        return (indices.size() > entity && indices.at(entity) < components.size());
    }

    void remove(uint32_t entity) override
    {
        uint32_t& index = indices.at(entity);
        components.at(index) = components.back();
        entities.at(index) = entities.back();
        indices.at(entities.at(index)) = index;
        index = std::numeric_limits<uint32_t>::max();
    }

    ComponentType& get(uint32_t entity)
    {
        return components.at(indices.at(entity));
    }

    ComponentType& add(uint32_t entity)
    {
        if (indices.size() <= entity)
        {
            indices.resize(entity + 1, std::numeric_limits<uint32_t>::max());
        }
        indices.at(entity) = components.size();
        entities.push_back(entity);
        return components.emplace_back();
    }

    template<typename Callable>
    void forEach(Callable&& fn)
    {
        for (uint32_t i = 0; i < components.size(); ++i)
        {
            fn(components.at(i), entities.at(i));
        }
    }
};

struct Entity
{
    constexpr static uint32_t Invalid = std::numeric_limits<uint32_t>::max();
    uint32_t x, y;
    bool alive;
};

struct Sprite
{
    uint32_t textureIndex;
    glm::vec4 color;
};

struct Cell
{
    bool solid = false;
    std::vector<uint32_t> occupants;
};

struct Map
{
    std::vector<std::vector<Cell>> cells;
};

enum class Direction
{
    Up, Left, Down, Right,
};

struct Enemy
{
    uint32_t entity;
    std::pair<uint32_t, uint32_t> target;
    Direction facingDirection;
};

struct Player {};
struct Follower {};

struct InputEvent
{
    Direction direction;
};

struct GameLogic final : eng::GameLogicInterface
{
    struct
    {
        uint32_t blank;
    } textures;

    std::map<Direction, uint32_t> directionInputMappings;

    std::map<size_t, std::unique_ptr<ComponentArrayBase>> componentArrays;

    Map map;
    std::vector<Entity> entities;
    std::deque<uint32_t> freeEntities;
    std::vector<uint32_t> playerEntities;
    // std::vector<Enemy> enemies;
    std::vector<std::pair<uint32_t, uint32_t>> patrolPoints;

    double tickTimer = 0;
    static constexpr double tickInterval = 0.5;
    std::deque<InputEvent> inputQueue;

    template<typename ComponentType>
    ComponentArray<ComponentType>& component()
    {
        auto typeHash = typeid(ComponentType).hash_code();
        auto it = componentArrays.find(typeHash);
        if (it == componentArrays.end())
        {
            bool inserted;
            std::tie(it, inserted) = componentArrays.emplace(typeHash, std::make_unique<ComponentArray<ComponentType>>());
            if (!inserted)
            {
                throw std::runtime_error("Failed to insert component type");
            }
        }
        return static_cast<ComponentArray<ComponentType>&>(*it->second);
    }

    uint32_t createEntity(uint32_t x, uint32_t y)
    {
        uint32_t index;
        if (!freeEntities.empty())
        {
            index = freeEntities.front();
            freeEntities.pop_front();
        }
        else
        {
            index = entities.size();
            entities.emplace_back();
        }
        entities.at(index) = Entity{ .x = x, .y = y, .alive = true };
        map.cells[y][x].occupants.push_back(index);
        return index;
    }

    void destroyEntity(uint32_t index)
    {
        Entity& entity = entities.at(index);
        if (entity.alive)
        {
            for (auto&& [_, componentArray] : componentArrays)
            {
                if (componentArray->has(index))
                {
                    componentArray->remove(index);
                }
            }

            auto& cell = map.cells[entities[index].y][entities[index].x];
            cell.occupants.erase(std::find(cell.occupants.begin(), cell.occupants.end(), index));

            entity.alive = false;
            freeEntities.push_back(index);
        }
        else
        {
            std::cerr << "entity already destroyed" << std::endl;
        }
    }

    void initPlayer(const std::initializer_list<std::pair<uint32_t, uint32_t>>& positions)
    {
        playerEntities.reserve(positions.size());
        auto it = positions.begin();
        if (it != positions.end())
        {
            auto&& [x, y] = *it;
            uint32_t entity = createEntity(x, y);
            playerEntities.push_back(entity);
            component<Player>().add(entity);
            component<Sprite>().add(entity) = { .textureIndex = textures.blank, .color = glm::vec4(1, 1, 0, 1) };
            ++it;
        }
        for (; it != positions.end(); ++it)
        {
            auto&& [x, y] = *it;
            uint32_t entity = createEntity(x, y);
            playerEntities.push_back(entity);
            component<Follower>().add(entity);
            component<Sprite>().add(entity) = { .textureIndex = textures.blank, .color = glm::vec4(0, 1, 1, 1) };
        }
    }

    // void createEnemy(uint32_t x, uint32_t y, const std::vector<std::pair<uint32_t, uint32_t>>& trackPoints)
    void createEnemy(uint32_t x, uint32_t y, Direction facingDirection)
    {
        uint32_t entity = createEntity(x, y);
        component<Enemy>().add(entity) = { .target = { x, y }, .facingDirection = facingDirection };
        component<Sprite>().add(entity) = { .textureIndex = textures.blank, .color = glm::vec4(1, 0, 0, 1) };
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
                    case 'T':
                        patrolPoints.push_back({ col, row });
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
                // createEnemy(x, y, { {x, y+1}, {x-2, y+1}, {x-2, y-1}, {x, y-1} });
                createEnemy(x, y, Direction::Down);
            }
        }

        directionInputMappings[Direction::Up] = input.createMapping();
        directionInputMappings[Direction::Left] = input.createMapping();
        directionInputMappings[Direction::Down] = input.createMapping();
        directionInputMappings[Direction::Right] = input.createMapping();

        input.mapKey(directionInputMappings[Direction::Up], glfwGetKeyScancode(GLFW_KEY_W));
        input.mapKey(directionInputMappings[Direction::Left], glfwGetKeyScancode(GLFW_KEY_A));
        input.mapKey(directionInputMappings[Direction::Down], glfwGetKeyScancode(GLFW_KEY_S));
        input.mapKey(directionInputMappings[Direction::Right], glfwGetKeyScancode(GLFW_KEY_D));

        scene.projection() = glm::orthoLH_ZO(0.0f, 40.0f, 24.0f, 0.0f, 0.0f, 1.0f);
    }

    void gameTick()
    {
        if (!inputQueue.empty())
        {
            InputEvent event = inputQueue.front();
            inputQueue.pop_front();

            int dx = 0, dy = 0;
            switch (event.direction)
            {
                case Direction::Up:
                    dy = -1;
                    break;
                case Direction::Left:
                    dx = -1;
                    break;
                case Direction::Down:
                    dy = 1;
                    break;
                case Direction::Right:
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

        component<Enemy>().forEach([this](Enemy& enemy, uint32_t id) {
            auto& entity = entities.at(id);                    
            auto [px, py] = enemy.target;
            if (entity.x == px && entity.y == py)
            {
                bool foundTarget = false;
                uint32_t targetIndex = 0;
                for (uint32_t i = 0; i < patrolPoints.size(); ++i)
                {

                }
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
        });
    }

    void runFrame(eng::SceneInterface& scene, eng::InputInterface& input, const double deltaTime) override
    {
        for (auto&& [ direction, mapping ] : directionInputMappings)
        {
            if (input.getBoolean(mapping, eng::InputInterface::BoolStateEvent::Pressed))
            {
                inputQueue.push_back(InputEvent { direction });
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

        component<Sprite>().forEach([&](const auto& sprite, auto& id)
        {
            const auto& entity = entities.at(id);
            scene.instances().push_back(eng::Instance {
                        .position = glm::vec2(1 + 2 * entity.x, 2 * (map.cells.size() - entity.y) - 1),
                        .scale = glm::vec2(1, 1),
                        .textureIndex = sprite.textureIndex,
                        .tintColor = sprite.color,
                    });
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
