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
    uint32_t prevx, prevy;
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
    uint32_t x, y;
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
    State prevState = State::Patrolling;
    uint32_t frame = 0;
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

struct Solid {};

struct InputEvent
{
    Direction direction;
};

struct InputIcon
{
    bool completed = false;
};

struct Text
{
    std::string text;
    glm::vec2 scale = { 1.0, 1.0 };
    glm::vec4 background = { 0, 0, 0, 0 };
    glm::vec4 foreground = { 1, 1, 1, 1 };
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
        default:
            return { 0, 0 };
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
        default:
            return 0;
    }
}

struct GameLogic final : eng::GameLogicInterface
{
    struct
    {
        uint32_t blank;
        std::map<Direction, uint32_t> friendly;
        struct
        {
            uint32_t backDown;
            uint32_t backUp;
            uint32_t frontDown;
            uint32_t frontUp;
            uint32_t frontUpBlink;
            uint32_t leftDown;
            uint32_t leftUp;
            uint32_t leftUpBlink;
            uint32_t rightDown;
            uint32_t rightUp;
            uint32_t rightUpBlink;
        } enemy;
        std::vector<uint32_t> dotline;
        std::vector<uint32_t> zap;
        uint32_t arrow;
        uint32_t font;
    } textures;

    struct
    {
        std::map<Direction, std::vector<uint32_t>> enemy;
    } sequences;

    std::map<Direction, uint32_t> directionInputMappings;

    std::map<size_t, std::unique_ptr<ComponentArrayBase>> componentArrays;

    Map map;
    std::vector<Entity> entities;
    std::deque<uint32_t> freeEntities;
    std::vector<uint32_t> playerEntities;

    static constexpr int animationFramesPerTick = 6;
    static constexpr int tweenFramesPerTick = 12;
    static constexpr double tickInterval = 0.5;
    static constexpr double animationFrameInterval = tickInterval / animationFramesPerTick;
    static constexpr double tweenFrameInterval = tickInterval / tweenFramesPerTick;
    double tickTimer = 0;
    double animationFrameTimer = 0;
    double tweenFrameTimer = 0;
    int tweenFrame = 0;
    float tween = 0;

    static constexpr int maxTilesVertical = 12;
    static constexpr int maxTilesHorizontal = 20;
    static constexpr int texelsPerTile = 32;

    std::deque<InputEvent> inputQueue;
    std::deque<uint32_t> inputSpriteEntities;

    uint32_t textTestEntity;

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
        entities.at(index) = Entity{ .x = x, .y = y, .prevx = x, .prevy = y, .alive = true };
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
            component<Solid>().add(entity);
            ++it;
        }
        for (; it != positions.end(); ++it)
        {
            auto&& [x, y] = *it;
            uint32_t entity = createEntity(x, y);
            playerEntities.push_back(entity);
            component<Friendly>().add(entity);
            component<Sprite>().add(entity);
            component<Solid>().add(entity);
        }
    }

    void createEnemy(uint32_t x, uint32_t y, Direction facingDirection)
    {
        uint32_t entity = createEntity(x, y);
        component<Enemy>().add(entity) = { .facingDirection = facingDirection };
        component<Sprite>().add(entity);
        component<Solid>().add(entity);
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
            newCell.occupants.push_back(id);
            Cell& oldCell = map.cells[entity.y][entity.x];
            oldCell.occupants.erase(std::find(oldCell.occupants.begin(), oldCell.occupants.end(), id));
            entity.x = x, entity.y = y;
        }
    }

    template<typename Callable>
    bool scan(uint32_t x, uint32_t y, Direction direction, uint32_t limit, Callable&& fn)
    {
        auto [dx, dy] = directionCoords(direction);
        clampDeltaToMap(x, y, dx, dy);
        uint32_t testx = x + dx, testy = y + dy;
        uint32_t distance = 0;
        while ((limit == 0 || distance < limit) && (dx != 0 || dy != 0))
        {
            ++distance;
            const auto& cell = map.cells[testy][testx];
            if (cell.solid)
            {
                return false;
            }
            if (fn(cell, distance))
            {
                return true;
            }
            if (std::find_if(cell.occupants.begin(), cell.occupants.end(),
                        [&](auto oid) { return component<Solid>().has(oid); }) != cell.occupants.end())
            {
                return false;
            }
            clampDeltaToMap(testx, testy, dx, dy);
            testx += dx, testy += dy;
        }
        return false;
    }

    void enemyLogic(Enemy& enemy, uint32_t id)
    {
        auto& entity = entities.at(id);
        enemy.prevState = enemy.state;

        // scan for player
        if (uint32_t target;
            scan(entity.x, entity.y, enemy.facingDirection, 0,
                [&](const Cell& cell, uint32_t distance)
                {
                    if (auto it = std::find_if(cell.occupants.begin(), cell.occupants.end(),
                                [&](auto oid){ return component<Friendly>().has(oid); });
                            it != cell.occupants.end())
                    {
                        target = *it;
                        return true;
                    }
                    return false;
                }))
        {
            // found player
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
                        ++it;
                    }
                    // else
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
            // not found player
            if (enemy.state == Enemy::State::Aggressive)
            {
                enemy.state = Enemy::State::Alert;
            }
            else
            {
                enemy.state = Enemy::State::Patrolling;
                bool validTarget = enemy.target.x < map.cells.front().size() && enemy.target.y < map.cells.size();
                if (validTarget)
                {
                    int toTargetX = (int)enemy.target.x - (int)entity.x;
                    int toTargetY = (int)enemy.target.y - (int)entity.y;
                    // did we reach it?
                    if (toTargetX == 0 && toTargetY == 0)
                    {
                        validTarget = false;
                    }
                    else
                    {
                        // are we facing correct direction?
                        auto [dx, dy] = directionCoords(enemy.facingDirection);
                        if (dx != glm::sign(toTargetX) || dy != glm::sign(toTargetY))
                        {
                            validTarget = false;
                        }
                        else
                        {
                            // are we about to run into a wall?
                            clampDeltaToMap(entity.x, entity.y, dx, dy);
                            uint32_t testx = entity.x + dx, testy = entity.y + dy;
                            const auto& cell = map.cells[testy][testx];
                            if (cell.solid || std::find_if(cell.occupants.begin(), cell.occupants.end(),
                                        [&](auto oid) { return component<Solid>().has(oid); }) != cell.occupants.end())
                            {
                                validTarget = false;
                            }
                        }
                    }
                }

                bool shouldMoveForward = true;
                if (!validTarget)
                {
                    // scan in current direction, then each of the perpendicular directions
                    const std::array scanDirections {
                        enemy.facingDirection, 
                        static_cast<Direction>((static_cast<int>(enemy.facingDirection) + 1) % 4),
                        static_cast<Direction>((static_cast<int>(enemy.facingDirection) + 3) % 4),
                    };

                    uint32_t bestPriority = 0;
                    uint32_t bestDistance = 0;
                    int bestIndex = 0;

                    for (uint32_t i = 0; i < scanDirections.size(); ++i)
                    {
                        scan(entity.x, entity.y, scanDirections[i], 0,
                            [&](const Cell& cell, uint32_t distance)
                            {
                                uint32_t priority = 0;
                                for (auto oid : cell.occupants)
                                {
                                    if (component<Solid>().has(oid))
                                    {
                                        priority = component<Friendly>().has(oid) ? 2 : 0;
                                        break;
                                    }
                                    if (component<PatrolPoint>().has(oid))
                                    {
                                        priority = 1;
                                        // do not break so we can make sure not obstructed
                                    }
                                }
                                if (priority > bestPriority || (priority > 0 && priority == bestPriority && distance < bestDistance))
                                {
                                    bestPriority = priority;
                                    bestDistance = distance;
                                    bestIndex = i;
                                }
                                else if (bestPriority == 0 && distance > bestDistance)
                                {
                                    bestDistance = distance;
                                    bestIndex = i;
                                }
                                return false;
                            });
                    }

                    shouldMoveForward = (scanDirections[bestIndex] == enemy.facingDirection);
                    enemy.facingDirection = scanDirections[bestIndex];
                    auto [dx, dy] = directionCoords(enemy.facingDirection);
                    enemy.target = { entity.x + bestDistance * dx, entity.y + bestDistance * dy };
                }

                if (shouldMoveForward)
                {
                    auto [dx, dy] = directionCoords(enemy.facingDirection);
                    clampDeltaToMap(entity.x, entity.y, dx, dy);
                    moveEntity(id, entity.x + dx, entity.y + dy);
                }
            }
        }
        if (enemy.prevState != enemy.state)
        {
            enemy.lineFrame = 0;
        }
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
            .backDown = resourceLoader.loadTexture("textures/NNBackDown.png"),
            .backUp = resourceLoader.loadTexture("textures/NNBackUp.png"),
            .frontDown = resourceLoader.loadTexture("textures/NNFrontDown.png"),
            .frontUp = resourceLoader.loadTexture("textures/NNFrontUp.png"),
            .frontUpBlink = resourceLoader.loadTexture("textures/NNFrontUpBlink.png"),
            .leftDown = resourceLoader.loadTexture("textures/NNLeftDown.png"),
            .leftUp = resourceLoader.loadTexture("textures/NNLeftUp.png"),
            .leftUpBlink = resourceLoader.loadTexture("textures/NNLeftUpBlink.png"),
            .rightDown = resourceLoader.loadTexture("textures/NNRightDown.png"),
            .rightUp = resourceLoader.loadTexture("textures/NNRightUp.png"),
            .rightUpBlink = resourceLoader.loadTexture("textures/NNRightUpBlink.png"),
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
        textures.arrow = resourceLoader.loadTexture("textures/arrow.png");
        textures.font = resourceLoader.loadTexture("textures/font.png");

        sequences.enemy = {
            { Direction::Up, {
                    textures.enemy.backDown,
                    textures.enemy.backDown,
                    textures.enemy.backUp,
                    textures.enemy.backUp,
                    textures.enemy.backUp,
                    textures.enemy.backDown,
                } },
            { Direction::Left, {
                    textures.enemy.leftDown,
                    textures.enemy.leftDown,
                    textures.enemy.leftUp,
                    textures.enemy.leftUp,
                    textures.enemy.leftUp,
                    textures.enemy.leftDown,
                    textures.enemy.leftDown,
                    textures.enemy.leftDown,
                    textures.enemy.leftUp,
                    textures.enemy.leftUpBlink,
                    textures.enemy.leftUp,
                    textures.enemy.leftDown,
                } },
            { Direction::Down, {
                    textures.enemy.frontDown,
                    textures.enemy.frontDown,
                    textures.enemy.frontUp,
                    textures.enemy.frontUp,
                    textures.enemy.frontUp,
                    textures.enemy.frontDown,
                    textures.enemy.frontDown,
                    textures.enemy.frontDown,
                    textures.enemy.frontUp,
                    textures.enemy.frontUpBlink,
                    textures.enemy.frontUp,
                    textures.enemy.frontDown,
                } },
            { Direction::Right, {
                    textures.enemy.rightDown,
                    textures.enemy.rightDown,
                    textures.enemy.rightUp,
                    textures.enemy.rightUp,
                    textures.enemy.rightUp,
                    textures.enemy.rightDown,
                    textures.enemy.rightDown,
                    textures.enemy.rightDown,
                    textures.enemy.rightUp,
                    textures.enemy.rightUpBlink,
                    textures.enemy.rightUp,
                    textures.enemy.rightDown,
                } },
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
                map.cells[row][col] = Cell { .x = col, .y = row, };
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

        textTestEntity = createEntity(0, 0);
        component<Text>().add(textTestEntity) = Text{
            .text = "GubGubs",
            .foreground = { 0.8, 0.2, 0.0, 1 },
        };
    }

    void gameTick()
    {
        for (auto& entity : entities)
        {
            entity.prevx = entity.x;
            entity.prevy = entity.y;
        }

        if (!inputSpriteEntities.empty() && component<InputIcon>().get(inputSpriteEntities.front()).completed)
        {
            destroyEntity(inputSpriteEntities.front());
            inputSpriteEntities.pop_front();
        }
        if (!inputQueue.empty())
        {
            InputEvent event = inputQueue.front();
            inputQueue.pop_front();
            component<InputIcon>().get(inputSpriteEntities.front()).completed = true;
            moveEntity(inputSpriteEntities.front(), entities[inputSpriteEntities.front()].x, entities[inputSpriteEntities.front()].y - 1);

            for (uint32_t i = 1; i < inputSpriteEntities.size(); ++i)
            {
                auto id = inputSpriteEntities[i];
                moveEntity(id, entities[id].x + 1, entities[id].y);
            }

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
                        if (component<Solid>().has(oid))
                        {
                            blocked = true;
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
                // inputQueue.clear();
                inputQueue.push_back(InputEvent { direction });
                uint32_t offset = inputSpriteEntities.size();
                if (!inputSpriteEntities.empty() && component<InputIcon>().get(inputSpriteEntities.front()).completed)
                {
                    --offset;
                }
                uint32_t id = createEntity(maxTilesHorizontal - 1 - offset, maxTilesVertical - 1);
                component<Sprite>().add(id) = Sprite{
                    .textureIndex = textures.arrow,
                    .color = { 1, 1, 0, 1 },
                    .direction = direction,
                };
                component<InputIcon>().add(id);
                inputSpriteEntities.push_back(id);
            }
        }

        if (animationFrameTimer >= animationFrameInterval)
        {
            component<Enemy>().forEach([&](Enemy& enemy, uint32_t id)
            {
                ++enemy.frame;
                ++enemy.lineFrame;

                auto& sprite = component<Sprite>().get(id);
                const auto& sequence = sequences.enemy.at(enemy.facingDirection);
                if (enemy.frame >= sequence.size())
                {
                    enemy.frame = 0;
                }
                sprite.textureIndex = sequence[enemy.frame];
            });

            animationFrameTimer -= animationFrameInterval;
        }
        animationFrameTimer += deltaTime;

        if (tickTimer >= tickInterval)
        {
            gameTick();
            tickTimer -= tickInterval;
            tweenFrame = 0;
        }
        tickTimer += deltaTime;

        if (tweenFrameTimer >= tweenFrameInterval)
        {
            constexpr int tweenEndFrame = tweenFramesPerTick / 2;
            tween = glm::clamp<float>(static_cast<float>(tweenFrame) / tweenEndFrame, 0, 1);
            tween = 3 * tween * tween - 2 * tween * tween * tween;
            tween = std::round(tween * texelsPerTile) / texelsPerTile;
            ++tweenFrame;

            if (!inputSpriteEntities.empty() && component<InputIcon>().get(inputSpriteEntities.front()).completed)
            {
                component<Sprite>().get(inputSpriteEntities.front()).color.a = 1.0f - tween;
            }

            tweenFrameTimer -= tweenFrameInterval;
        }
        tweenFrameTimer += deltaTime;


        scene.instances().clear();
        for (uint32_t i = 0; i < map.cells.size(); ++i)
        {
            for (uint32_t j = 0; j < map.cells[i].size(); ++j)
            {
                if (map.cells[i][j].solid)
                {
                    scene.instances().push_back(eng::Instance {
                                .position = glm::vec2(j + 0.5, maxTilesVertical - i - 0.5),
                                .textureIndex = textures.blank,
                            });
                }
            }
        }

        component<Sprite>().forEach([&](const auto& sprite, auto id)
        {
            const auto& entity = entities.at(id);
            glm::vec2 position = glm::mix(glm::vec2(entity.prevx + 0.5, maxTilesVertical - entity.prevy - 0.5),
                    glm::vec2(entity.x + 0.5, maxTilesVertical - entity.y - 0.5), tween);
            scene.instances().push_back(eng::Instance {
                        .position = position,
                        .angle = directionAngle(sprite.direction),
                        .textureIndex = sprite.textureIndex,
                        .tintColor = sprite.color,
                    });
        });

        component<Enemy>().forEach([&](Enemy& enemy, uint32_t id)
        {
            const auto& entity = entities[id];

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
            float angle = directionAngle(enemy.facingDirection) - glm::half_pi<float>();

            scan(entity.x, entity.y, enemy.facingDirection, 0,
                [&](const Cell& cell, uint32_t distance)
                {
                    if (auto it = std::find_if(cell.occupants.begin(), cell.occupants.end(),
                            [&](auto oid){ return component<Solid>().has(oid); });
                        it != cell.occupants.end())
                    {
                        if (!(component<Friendly>().has(*it) || component<Neutral>().has(*it)))
                        {
                            return true;
                        }
                    }
                    scene.instances().push_back(eng::Instance {
                                .position = glm::vec2(cell.x + 0.5, maxTilesVertical - cell.y - 0.5),
                                .angle = angle,
                                .textureIndex = textureIndex,
                                .tintColor = tintColor,
                            });
                    return false;
                });
        });

        component<Text>().forEach([&](const Text& text, uint32_t id)
        {
            const auto& entity = entities[id];
            constexpr glm::vec2 texCoordScale = { 1.0f / 16.0f, 1.0f / 8.0f };
            scene.instances().push_back(eng::Instance {
                    .position = { entity.x + 0.25f * text.text.size(), maxTilesVertical - entity.y - 0.5 },
                    .scale = { text.scale.x * 0.5f * text.text.size(), text.scale.y },
                    .textureIndex = textures.blank,
                    .tintColor = text.background,
                });
            for (uint32_t i = 0; i < text.text.size(); ++i)
            {
                glm::vec2 minTexCoord = glm::vec2(text.text[i] / 8, text.text[i] % 8) * texCoordScale;
                scene.instances().push_back(eng::Instance {
                        .position = { entity.x + i * 0.5 + 0.25, maxTilesVertical - entity.y - 0.5 },
                        .scale = { 0.5, 1.0 },
                        .minTexCoord = minTexCoord,
                        .texCoordScale = texCoordScale,
                        .textureIndex = textures.font,
                        .tintColor = text.foreground,
                    });
            }
        });


        const auto [framebufferWidth, framebufferHeight] = scene.framebufferSize();
        const float aspectRatio = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
        if (maxTilesVertical * aspectRatio > maxTilesHorizontal)
        {
            const float framebufferPixelsPerTile = static_cast<float>(framebufferHeight) / static_cast<float>(maxTilesVertical);
            const float viewportWidth = maxTilesHorizontal * framebufferPixelsPerTile;
            scene.viewportOffset() = { (static_cast<float>(framebufferWidth) - viewportWidth) / 2, 0 };
            scene.viewportExtent() = { viewportWidth, framebufferHeight };
        }
        else
        {
            const float framebufferPixelsPerTile = static_cast<float>(framebufferWidth) / static_cast<float>(maxTilesHorizontal);
            const float viewportHeight = maxTilesVertical * framebufferPixelsPerTile;
            scene.viewportOffset() = { 0, (static_cast<float>(framebufferHeight) - viewportHeight) / 2 };
            scene.viewportExtent() = { framebufferWidth, viewportHeight };
        }
        scene.projection() = glm::orthoLH_ZO<float>(0.0f, maxTilesHorizontal, maxTilesVertical, 0.0f, 0.0f, 1.0f);
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
            .windowWidth = 3 * GameLogic::texelsPerTile * GameLogic::maxTilesHorizontal,
            .windowHeight = 3 * GameLogic::texelsPerTile * GameLogic::maxTilesVertical,
        });
}
