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

enum class Direction
{
    Up, Left, Down, Right,
};

struct ComponentArrayBase
{
    virtual ~ComponentArrayBase() = default;

    virtual bool has(uint32_t) = 0;
    virtual void remove(uint32_t) = 0;
    virtual void clear() = 0;
    virtual void removeLater(uint32_t) = 0;
};

template<typename ComponentType>
struct ComponentArray : ComponentArrayBase
{
    std::vector<ComponentType> components;
    std::vector<uint32_t> entities;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> toRemove;

    bool has(uint32_t entity) override
    {
        return (indices.size() > entity && indices.at(entity) < components.size());
    }

    void remove(uint32_t entity) override
    {
        uint32_t& index = indices.at(entity);
        components.at(index) = std::move(components.back());
        entities.at(index) = entities.back();
        indices.at(entities.at(index)) = index;
        index = std::numeric_limits<uint32_t>::max();
        components.pop_back();
        entities.pop_back();
    }

    void clear() override
    {
        components.clear();
        entities.clear();
        indices.clear();
    }

    void removeLater(uint32_t id) override
    {
        toRemove.push_back(id);
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
        for (auto id : toRemove)
        {
            remove(id);
        }
        toRemove.clear();
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
    glm::vec4 color { 1, 1, 1, 1 };
    Direction direction = Direction::Down;
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

struct Enemy
{
    enum class State
    {
        Patrolling,
        Alert,
        Aggressive,
        Attack,
    };

    struct { uint32_t x = std::numeric_limits<uint32_t>::max(), y = std::numeric_limits<uint32_t>::max(); } target;
    Direction facingDirection = Direction::Down;
    State state = State::Patrolling;
    uint32_t lineFrame = 0;
};

struct Leader
{
    Direction facingDirection = Direction::Down;
};

struct Friendly {};

struct Neutral
{
    uint32_t cooldown = 3;
};

struct PatrolPoint {};

struct InputEvent
{
    Direction direction;
};

static constexpr std::pair<int, int> directionCoords(Direction direction)
{
    switch (direction)
    {
        case Direction::Up:
            return { 0, -1 };
        case Direction::Left:
            return { -1, 0 };
        case Direction::Down:
            return { 0, 1 };
        case Direction::Right:
            return { 1, 0 };
    }
}

static constexpr Direction directionFromDelta(int dx, int dy)
{
    if (dy < 0)
    {
        return Direction::Up;
    }
    if (dx < 0)
    {
        return Direction::Left;
    }
    if (dx > 0)
    {
        return Direction::Right;
    }
    return Direction::Down;
}

static constexpr float directionAngle(Direction direction)
{
    switch (direction)
    {
        case Direction::Up:
            return glm::pi<float>();
        case Direction::Left:
            return -glm::half_pi<float>();
        case Direction::Down:
            return 0;
        case Direction::Right:
            return glm::half_pi<float>();
    }
}

struct GameLogic final : eng::GameLogicInterface
{
    struct
    {
        uint32_t blank;
        std::map<Direction, uint32_t> friendly;
        std::map<Direction, uint32_t> enemy;
        std::vector<uint32_t> dotline;
        std::vector<uint32_t> zap;
    } textures;

    std::map<Direction, uint32_t> directionInputMappings;

    std::map<size_t, std::unique_ptr<ComponentArrayBase>> componentArrays;

    Map map;
    std::vector<Entity> entities;
    std::deque<uint32_t> freeEntities;
    std::vector<uint32_t> playerEntities;

    static constexpr double tickInterval = 0.5;
    static constexpr double animationFrameInterval = tickInterval / 6;
    double tickTimer = 0;
    double animationFrameTimer = 0;

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
            component<Friendly>().add(entity);
            component<Leader>().add(entity);
            component<Sprite>().add(entity) = { .color = glm::vec4(1, 1, 0, 1) };
            ++it;
        }
        for (; it != positions.end(); ++it)
        {
            auto&& [x, y] = *it;
            uint32_t entity = createEntity(x, y);
            playerEntities.push_back(entity);
            component<Friendly>().add(entity);
            component<Sprite>().add(entity);
        }
    }

    void createEnemy(uint32_t x, uint32_t y, Direction facingDirection)
    {
        uint32_t entity = createEntity(x, y);
        component<Enemy>().add(entity) = { .facingDirection = facingDirection };
        component<Sprite>().add(entity);
    }

    void clampDeltaToMap(uint32_t x, uint32_t y, int& dx, int& dy)
    {
        if (dx < 0 && x == 0)
        {
            dx = 0;
        }
        if (dx > 0 && x == map.cells[0].size() - 1)
        {
            dx = 0;
        }
        if (dy < 0 && y == 0)
        {
            dy = 0;
        }
        if (dy > 0 && y == map.cells.size() - 1)
        {
            dy = 0;
        }
    }

    void moveEntity(uint32_t id, uint32_t x, uint32_t y)
    {
        Entity& entity = entities.at(id);
        if ((entity.x != x || entity.y != y) && x < map.cells.front().size() && y < map.cells.size())
        {
            Cell& newCell = map.cells[y][x];
            if (!newCell.solid)
            {
                newCell.occupants.push_back(id);
                Cell& oldCell = map.cells[entity.y][entity.x];
                oldCell.occupants.erase(std::find(oldCell.occupants.begin(), oldCell.occupants.end(), id));
                entity.x = x;
                entity.y = y;
            }
        }
    }

    void enemyLogic(Enemy& enemy, uint32_t id)
    {
        auto& entity = entities.at(id);

        auto [dx, dy] = directionCoords(enemy.facingDirection);
        uint32_t testx = entity.x, testy = entity.y;
        clampDeltaToMap(testx, testy, dx, dy);
        uint32_t target = Entity::Invalid;

        // scan forwards for player entities
        while (dx != 0 || dy != 0)
        {
            testx += dx;
            testy += dy;

            const auto& cell = map.cells[testy][testx];
            if (cell.solid)
            {
                break;
            }

            if (auto it = std::find_if(cell.occupants.begin(), cell.occupants.end(), [&](auto oid) { return component<Friendly>().has(oid); });
                    it != cell.occupants.end())
            {
                target = *it;
                break;
            }
            clampDeltaToMap(testx, testy, dx, dy);
        }

        if (target != Entity::Invalid)
        {
            if (enemy.state == Enemy::State::Attack)
            {
                enemy.state = Enemy::State::Alert;
            }
            else
            {
                enemy.state = static_cast<Enemy::State>(static_cast<int>(enemy.state) + 1);
            }

            if (enemy.state == Enemy::State::Attack)
            {
                if (auto it = std::find(playerEntities.begin(), playerEntities.end(), target); it != playerEntities.end())
                {
                    if (it == playerEntities.begin())
                    {
                        // dead?
                    }
                    else
                    {
                        for (auto tmp = it; tmp != playerEntities.end(); ++tmp)
                        {
                            component<Friendly>().remove(*tmp);
                            component<Neutral>().add(*tmp);
                            component<Sprite>().get(*tmp).color = { 1, 0, 1, 1 };
                        }
                        playerEntities.erase(it, playerEntities.end());
                    }
                }
            }
        }
        else
        {
            if (enemy.state == Enemy::State::Aggressive)
            {
                enemy.state = Enemy::State::Alert;
            }
            else
            {
                enemy.state = Enemy::State::Patrolling;
                // do we have a valid target?
                if (enemy.target.x < map.cells.front().size() && enemy.target.y < map.cells.size())
                {
                    int toTargetX = (int)enemy.target.x - (int)entity.x;
                    int toTargetY = (int)enemy.target.y - (int)entity.y;
                    // did we reach it?
                    if (toTargetX == 0 && toTargetY == 0)
                    {
                        enemy.target = { std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max() };
                    }
                    else
                    {
                        // are we facing correct direction?
                        auto [dx, dy] = directionCoords(enemy.facingDirection);
                        if (dx != glm::sign(toTargetX) || dy != glm::sign(toTargetY))
                        {
                            enemy.target = { std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max() };
                        }
                        else
                        {
                            // are we about to run into a wall?
                            clampDeltaToMap(entity.x, entity.y, dx, dy);
                            uint32_t testx = entity.x + dx, testy = entity.y + dy;
                            const auto& cell = map.cells[testy][testx];
                            if (cell.solid)
                            {
                                enemy.target = { std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max() };
                            }
                        }
                    }
                }

                // do we still have a valid target?
                if (enemy.target.x < map.cells.front().size() && enemy.target.y < map.cells.size())
                {
                    auto [dx, dy] = directionCoords(enemy.facingDirection);
                    clampDeltaToMap(entity.x, entity.y, dx, dy);
                    moveEntity(id, entity.x + dx, entity.y + dy);
                }
                else
                {
                    // scan in current direction, then each of the perpendicular directions
                    const std::array scanDirections {
                        enemy.facingDirection, 
                        static_cast<Direction>((static_cast<int>(enemy.facingDirection) + 1) % 4),
                        static_cast<Direction>((static_cast<int>(enemy.facingDirection) + 3) % 4),
                    };

                    bool anyScanFound = false;
                    int bestDistance = 0;
                    int bestIndex = 0;

                    for (uint32_t i = 0; i < scanDirections.size(); ++i)
                    {
                        auto [dx, dy] = directionCoords(scanDirections[i]);
                        clampDeltaToMap(entity.x, entity.y, dx, dy);
                        uint32_t testx = entity.x + dx, testy = entity.y + dy;
                        int distance = 0;

                        while (dx != 0 || dy != 0)
                        {
                            const auto& cell = map.cells[testy][testx];
                            if (cell.solid)
                            {
                                if (!anyScanFound && distance > bestDistance)
                                {
                                    bestDistance = distance;
                                    bestIndex = i;
                                }
                                break;
                            }

                            ++distance;
                            if (std::find_if(cell.occupants.begin(), cell.occupants.end(), [&](auto oid) {
                                            return component<PatrolPoint>().has(oid);
                                        }) != cell.occupants.end())
                            {
                                if (!anyScanFound || distance < bestDistance)
                                {
                                    anyScanFound = true;
                                    bestDistance = distance;
                                    bestIndex = i;
                                }
                                break;
                            }

                            clampDeltaToMap(testx, testy, dx, dy);
                            testx += dx;
                            testy += dy;
                        }
                    }

                    enemy.facingDirection = scanDirections[bestIndex];
                    auto [dx, dy] = directionCoords(enemy.facingDirection);
                    enemy.target = { entity.x + bestDistance * dx, entity.y + bestDistance * dy };
                }
            }
        }

        auto& sprite = component<Sprite>().get(id);
        sprite.textureIndex = textures.enemy.at(enemy.facingDirection);
    }

    void init(eng::ResourceLoaderInterface& resourceLoader, eng::SceneInterface& scene, eng::InputInterface& input) override
    {
        textures.blank = resourceLoader.loadTexture("textures/blank.png");
        textures.friendly = {
            { Direction::Up, resourceLoader.loadTexture("textures/GubgubSpriteBack.png") },
            { Direction::Left, resourceLoader.loadTexture("textures/GubgubSpriteSideLeft.png") },
            { Direction::Down, resourceLoader.loadTexture("textures/GubgubSpriteFront.png") },
            { Direction::Right, resourceLoader.loadTexture("textures/GubgubSpriteSideRight.png") },
        };
        textures.enemy = {
            { Direction::Up, resourceLoader.loadTexture("textures/NubnubSpriteBack.png") },
            { Direction::Left, resourceLoader.loadTexture("textures/NubnubSpriteSideLeft.png") },
            { Direction::Down, resourceLoader.loadTexture("textures/NubnubSpriteFront.png") },
            { Direction::Right, resourceLoader.loadTexture("textures/NubnubSpriteSideRight.png") },
        };
        textures.dotline = {
            resourceLoader.loadTexture("textures/Dotline1.png"),
            resourceLoader.loadTexture("textures/Dotline2.png"),
            resourceLoader.loadTexture("textures/Dotline3.png"),
            resourceLoader.loadTexture("textures/Dotline4.png"),
        };
        textures.zap = {
            resourceLoader.loadTexture("textures/Zap1.png"),
            resourceLoader.loadTexture("textures/Zap2.png"),
            resourceLoader.loadTexture("textures/Zap3.png"),
            resourceLoader.loadTexture("textures/Zap4.png"),
            resourceLoader.loadTexture("textures/Zap5.png"),
            resourceLoader.loadTexture("textures/Zap6.png"),
        };

        using namespace std::string_view_literals;
        constexpr std::array mapRows {
            "XXXXXXXXXXXXXXXXXXXX"sv,
            "XT______T_T_______TX"sv,
            "X________XE________X"sv,
            "X__T_T__T_T________X"sv,
            "X___XE________T_T__X"sv,
            "X__T_T_________XE__X"sv,
            "X_____________T_T__X"sv,
            "X________P_________X"sv,
            "X__________________X"sv,
            "X__________________X"sv,
            "XT________________TX"sv,
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
                createEnemy(x, y, Direction::Down);
            }
        }

        if (auto it = markers.find('T'); it != markers.end())
        {
            for (auto&& [x, y] : it->second)
            {
                auto id = createEntity(x, y);
                component<PatrolPoint>().add(id);
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

            if (!playerEntities.empty())
            {
                Entity& player = entities[playerEntities.front()];
                component<Leader>().get(playerEntities.front()).facingDirection = event.direction;

                auto [dx, dy] = directionCoords(event.direction);
                clampDeltaToMap(player.x, player.y, dx, dy);

                const auto& cell = map.cells[player.y + dy][player.x + dx];
                if (!cell.solid)
                {
                    bool blocked = false;
                    bool attack = false;
                    bool capture = false;
                    uint32_t target = Entity::Invalid;
                    for (const auto oid : cell.occupants)
                    {
                        if (component<Friendly>().has(oid))
                        {
                            blocked = true;
                            break;
                        }
                        if (component<Enemy>().has(oid))
                        {
                            attack = true;
                            target = oid;
                            break;
                        }
                        if (component<Neutral>().has(oid))
                        {
                            capture = true;
                            target = oid;
                            break;
                        }
                    }

                    if (!blocked)
                    {
                        if (attack)
                        {
                            component<Enemy>().remove(target);
                            component<Neutral>().add(target);
                            auto& sprite = component<Sprite>().get(target);
                            sprite.textureIndex = textures.friendly[Direction::Down];
                            sprite.color = { 1, 0, 1, 1 };
                        }
                        else
                        {
                            if (capture)
                            {
                                component<Neutral>().remove(target);
                                component<Friendly>().add(target);
                                auto& sprite = component<Sprite>().get(target);
                                sprite.color = { 1, 1, 1, 1 };
                                playerEntities.push_back(target);
                            }

                            for (uint32_t i = playerEntities.size() - 1; i > 0; --i)
                            {
                                auto& nextEntity = entities[playerEntities[i - 1]];
                                moveEntity(playerEntities[i], nextEntity.x, nextEntity.y);
                            }

                            moveEntity(playerEntities.front(), player.x + dx, player.y + dy);
                        }
                    }

                }
            }
        }

        if (!playerEntities.empty())
        {
            component<Sprite>().get(playerEntities.front()).textureIndex = textures.friendly.at(component<Leader>().get(playerEntities.front()).facingDirection);
            for (uint32_t i = 1; i < playerEntities.size(); ++i)
            {
                auto& entity = entities[playerEntities[i]];
                auto& nextEntity = entities[playerEntities[i - 1]];
                auto direction = directionFromDelta((int)nextEntity.x - (int)entity.x, (int)nextEntity.y - (int)entity.y);
                component<Sprite>().get(playerEntities[i]).textureIndex = textures.friendly.at(direction);
            }
        }

        component<Neutral>().forEach([this](Neutral& neutral, uint32_t id)
        {
            if (neutral.cooldown == 0)
            {
                component<Enemy>().add(id);
                component<Sprite>().get(id).color = { 1, 1, 1, 1 };
                component<Neutral>().removeLater(id);
            }
            else
            {
                --neutral.cooldown;
            }
        });

        component<Enemy>().forEach([this](Enemy& enemy, uint32_t id)
        {
            enemyLogic(enemy, id);
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

        if (animationFrameTimer >= animationFrameInterval)
        {
            component<Enemy>().forEach([&](Enemy& enemy, uint32_t)
            {
                ++enemy.lineFrame;
            });
            animationFrameTimer -= animationFrameInterval;
        }
        animationFrameTimer += deltaTime;

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
                                .textureIndex = textures.blank,
                            });
                }
            }
        }

        component<Sprite>().forEach([&](const auto& sprite, auto id)
        {
            const auto& entity = entities.at(id);
            scene.instances().push_back(eng::Instance {
                        .position = glm::vec2(1 + 2 * entity.x, 2 * (map.cells.size() - entity.y) - 1),
                        .angle = directionAngle(sprite.direction),
                        .textureIndex = sprite.textureIndex,
                        .tintColor = sprite.color,
                    });
        });

        component<Enemy>().forEach([&](Enemy& enemy, uint32_t id)
        {
            const auto& entity = entities[id];
            auto [dx, dy] = directionCoords(enemy.facingDirection);
            clampDeltaToMap(entity.x, entity.y, dx, dy);
            uint32_t x = entity.x + dx, y = entity.y + dy;
            while (dx != 0 || dy != 0)
            {
                if (map.cells[y][x].solid)
                {
                    break;
                }
                uint32_t textureIndex;
                glm::vec4 tintColor = { 1, 1, 1, 1 };
                if (enemy.state == Enemy::State::Attack)
                {
                    if (enemy.lineFrame >= textures.zap.size())
                    {
                        enemy.lineFrame = 0;
                    }
                    textureIndex = textures.zap[enemy.lineFrame];
                }
                else
                {
                    if (enemy.lineFrame >= textures.dotline.size())
                    {
                        enemy.lineFrame = 0;
                    }
                    textureIndex = textures.dotline[enemy.lineFrame];
                    if (enemy.state == Enemy::State::Alert)
                    {
                        tintColor = { 1, 1, 0, 1 };
                    }
                    else if (enemy.state == Enemy::State::Aggressive)
                    {
                        tintColor = { 1, 0, 0, 1 };
                    }
                }
                scene.instances().push_back(eng::Instance {
                            .position = glm::vec2(1 + 2 * x, 2 * (map.cells.size() - y) - 1),
                            .angle = directionAngle(enemy.facingDirection) - glm::half_pi<float>(),
                            .textureIndex = textureIndex,
                            .tintColor = tintColor,
                        });

                clampDeltaToMap(x, y, dx, dy);
                x += dx, y +=  dy;
            }
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
