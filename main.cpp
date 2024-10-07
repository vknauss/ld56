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
};

struct MapCoords
{
    uint32_t x = std::numeric_limits<uint32_t>::max();
    uint32_t y = std::numeric_limits<uint32_t>::max();
};

struct Sprite
{
    uint32_t textureIndex;
    glm::vec4 color { 1, 1, 1, 1 };
    bool flipHorizontal = false;
    Direction direction = Direction::Down;
    uint32_t x = std::numeric_limits<uint32_t>::max();
    uint32_t y = std::numeric_limits<uint32_t>::max();
    uint32_t prevx = std::numeric_limits<uint32_t>::max();
    uint32_t prevy = std::numeric_limits<uint32_t>::max();
};

struct Cell
{
    uint32_t x, y;
    bool solid = false;
    std::vector<uint32_t> occupants;
};

struct CharacterTextureSet
{
    std::vector<uint32_t> front;
    std::vector<uint32_t> back;
    std::vector<uint32_t> side;
};

struct CharacterAnimator
{
    const CharacterTextureSet* textureSet = nullptr;
    Direction direction = Direction::Down;
};

struct SequenceAnimator
{
    const std::vector<uint32_t>* sequence = nullptr;
    uint32_t frame = 0;
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
    uint32_t lineFrame = 0;
};

struct Friendly {};

struct Neutral
{
    bool wasFriendly;
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
    glm::vec2 position;
};

struct Door
{
    bool open;
};

struct Transient {};

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

struct Map
{
    std::vector<std::string_view> rows;
    uint32_t entitiesNeeded;
    std::vector<std::string> levelText;
};

using namespace std::string_view_literals;

struct GameLogic final : eng::GameLogicInterface
{
    std::vector<Map> maps;
    uint32_t currentLevel = 0;

    struct
    {
        uint32_t blank;
        CharacterTextureSet enemy;
        CharacterTextureSet friendly;
        CharacterTextureSet leader;
        std::vector<uint32_t> sightline;
        std::vector<uint32_t> sightlineEnd;
        std::vector<uint32_t> zap;
        std::vector<uint32_t> zapHit;
        std::vector<uint32_t> bonk;
        std::vector<uint32_t> enemySleepy;
        std::vector<uint32_t> friendlySleepy;
        std::vector<uint32_t> transform;
        uint32_t arrow;
        uint32_t font;
        uint32_t wall;
        uint32_t floor;
    } textures;

    std::map<Direction, uint32_t> directionInputMappings;

    std::map<size_t, std::unique_ptr<ComponentArrayBase>> componentArrays;

    std::vector<std::vector<Cell>> cells;
    std::deque<uint32_t> freeEntities;
    std::vector<uint32_t> playerEntities;
    uint32_t entityIndexCounter = 0;

    uint32_t entitiesNeeded;

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

    glm::vec2 mapViewCenter;
    glm::vec2 prevMapViewCenter;

    static constexpr int maxTilesVertical = 12;
    static constexpr int maxTilesHorizontal = 20;
    static constexpr int texelsPerTile = 32;

    std::deque<InputEvent> inputQueue;
    std::deque<uint32_t> inputSpriteEntities;

    uint32_t gubgubCounterText;

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

    uint32_t createEntity()
    {
        uint32_t index;
        if (!freeEntities.empty())
        {
            index = freeEntities.front();
            freeEntities.pop_front();
        }
        else
        {
            index = entityIndexCounter++;
        }
        return index;
    }

    void destroyEntity(uint32_t index)
    {
        if (component<MapCoords>().has(index))
        {
            const auto& mapCoords = component<MapCoords>().get(index);
            auto& cell = cells[mapCoords.y][mapCoords.x];
            cell.occupants.erase(std::find(cell.occupants.begin(), cell.occupants.end(), index));
        }
        for (auto&& [_, componentArray] : componentArrays)
        {
            if (componentArray->has(index))
            {
                componentArray->remove(index);
            }
        }

        freeEntities.push_back(index);
    }

    void initPlayer(const std::initializer_list<std::pair<uint32_t, uint32_t>>& positions)
    {
        playerEntities.reserve(positions.size());
        auto it = positions.begin();
        if (it != positions.end())
        {
            auto&& [x, y] = *it;
            uint32_t entity = createEntity();
            playerEntities.push_back(entity);
            component<MapCoords>().add(entity);
            component<Friendly>().add(entity);
            component<Sprite>().add(entity);
            component<Solid>().add(entity);
            component<CharacterAnimator>().add(entity) = {
                .textureSet = &textures.leader,
            };
            component<SequenceAnimator>().add(entity);
            moveEntity(entity, x, y);
            ++it;
        }
        for (; it != positions.end(); ++it)
        {
            auto&& [x, y] = *it;
            uint32_t entity = createEntity();
            playerEntities.push_back(entity);
            component<MapCoords>().add(entity);
            component<Friendly>().add(entity);
            component<Sprite>().add(entity);
            component<Solid>().add(entity);
            component<CharacterAnimator>().add(entity) = {
                .textureSet = &textures.friendly,
            };
            component<SequenceAnimator>().add(entity);
            moveEntity(entity, x, y);
        }
    }

    void createEnemy(uint32_t x, uint32_t y, Direction facingDirection)
    {
        uint32_t entity = createEntity();
        component<MapCoords>().add(entity);
        component<Enemy>().add(entity) = { .facingDirection = facingDirection };
        component<Sprite>().add(entity);
        component<Solid>().add(entity);
        component<CharacterAnimator>().add(entity) = {
            .textureSet = &textures.enemy,
        };
        component<SequenceAnimator>().add(entity);
        moveEntity(entity, x, y);
    }

    void clampDeltaToMap(uint32_t x, uint32_t y, int& dx, int& dy)
    {
        if (dx < 0 && x == 0)
        {
            dx = 0;
        }
        if (dx > 0 && x == cells[0].size() - 1)
        {
            dx = 0;
        }
        if (dy < 0 && y == 0)
        {
            dy = 0;
        }
        if (dy > 0 && y == cells.size() - 1)
        {
            dy = 0;
        }
    }

    void moveEntity(uint32_t id, uint32_t x, uint32_t y)
    {
        auto& mapCoords = component<MapCoords>().get(id);
        if ((mapCoords.x != x || mapCoords.y != y) && x < cells.front().size() && y < cells.size())
        {
            Cell& newCell = cells[y][x];
            newCell.occupants.push_back(id);
            if (mapCoords.x < cells.front().size() && mapCoords.y < cells.size())
            {
                Cell& oldCell = cells[mapCoords.y][mapCoords.x];
                oldCell.occupants.erase(std::find(oldCell.occupants.begin(), oldCell.occupants.end(), id));
            }
            mapCoords.x = x, mapCoords.y = y;
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
            const auto& cell = cells[testy][testx];
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
        const auto& mapCoords = component<MapCoords>().get(id);
        enemy.prevState = enemy.state;

        // scan for player
        if (uint32_t target;
            scan(mapCoords.x, mapCoords.y, enemy.facingDirection, 0,
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
                            component<Neutral>().add(*tmp).wasFriendly = true;
                            component<CharacterAnimator>().remove(*tmp);
                            component<SequenceAnimator>().get(*tmp).sequence = &textures.friendlySleepy;
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
                bool validTarget = enemy.target.x < cells.front().size() && enemy.target.y < cells.size();
                if (validTarget)
                {
                    int toTargetX = (int)enemy.target.x - (int)mapCoords.x;
                    int toTargetY = (int)enemy.target.y - (int)mapCoords.y;
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
                            clampDeltaToMap(mapCoords.x, mapCoords.y, dx, dy);
                            uint32_t testx = mapCoords.x + dx, testy = mapCoords.y + dy;
                            const auto& cell = cells[testy][testx];
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
                        scan(mapCoords.x, mapCoords.y, scanDirections[i], 0,
                            [&](const Cell& cell, uint32_t distance)
                            {
                                uint32_t priority = 0;
                                bool blocked = false;
                                for (auto oid : cell.occupants)
                                {
                                    if (component<Solid>().has(oid))
                                    {
                                        if (component<Friendly>().has(oid))
                                        {
                                            priority = 2;
                                        }
                                        else
                                        {
                                            priority = 0;
                                            blocked = true;
                                        }
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
                                else if (bestPriority == 0 && !blocked && distance > bestDistance)
                                {
                                    bestDistance = distance;
                                    bestIndex = i;
                                }
                                return false;
                            });
                    }

                    shouldMoveForward = (bestDistance > 0 && scanDirections[bestIndex] == enemy.facingDirection);
                    enemy.facingDirection = scanDirections[bestIndex];
                    auto [dx, dy] = directionCoords(enemy.facingDirection);
                    enemy.target = { mapCoords.x + bestDistance * dx, mapCoords.y + bestDistance * dy };
                }

                if (shouldMoveForward)
                {
                    auto [dx, dy] = directionCoords(enemy.facingDirection);
                    clampDeltaToMap(mapCoords.x, mapCoords.y, dx, dy);
                    moveEntity(id, mapCoords.x + dx, mapCoords.y + dy);
                }
            }
        }
        if (enemy.prevState != enemy.state)
        {
            enemy.lineFrame = 0;
        }
        component<CharacterAnimator>().get(id).direction = enemy.facingDirection;
    }

    void loadEnemyTextures(eng::ResourceLoaderInterface& resourceLoader)
    {
        uint32_t backDown1 = resourceLoader.loadTexture("textures/NNBackDown1.png");
        uint32_t backDown2 = resourceLoader.loadTexture("textures/NNBackDown2.png");
        uint32_t backUp1 = resourceLoader.loadTexture("textures/NNBackUp1.png");
        uint32_t backUp2 = resourceLoader.loadTexture("textures/NNBackUp2.png");
        uint32_t frontDown1 = resourceLoader.loadTexture("textures/NNFrontDown1.png");
        uint32_t frontDown2 = resourceLoader.loadTexture("textures/NNFrontDown2.png");
        uint32_t frontUp1 = resourceLoader.loadTexture("textures/NNFrontUp1.png");
        uint32_t frontUp2 = resourceLoader.loadTexture("textures/NNFrontUp2.png");
        uint32_t frontUp2Blink = resourceLoader.loadTexture("textures/NNFrontUp2Blink.png");
        uint32_t sideDown1 = resourceLoader.loadTexture("textures/NNSideDown1.png");
        uint32_t sideDown2 = resourceLoader.loadTexture("textures/NNSideDown2.png");
        uint32_t sideUp1 = resourceLoader.loadTexture("textures/NNSideUp1.png");
        uint32_t sideUp2 = resourceLoader.loadTexture("textures/NNSideUp2.png");
        uint32_t sideUp2Blink = resourceLoader.loadTexture("textures/NNSideUp2Blink.png");

        textures.enemy = {
            .front = {
                frontDown1,
                frontDown2,
                frontDown2,
                frontUp1,
                frontUp2,
                frontUp2,
                frontDown1,
                frontDown2,
                frontDown2,
                frontUp1,
                frontUp2Blink,
                frontUp2,
            },
            .back = {
                backDown1,
                backDown2,
                backDown2,
                backUp1,
                backUp2,
                backUp2,
            },
            .side = {
                sideDown1,
                sideDown2,
                sideDown2,
                sideUp1,
                sideUp2,
                sideUp2,
                sideDown1,
                sideDown2,
                sideDown2,
                sideUp1,
                sideUp2Blink,
                sideUp2,
            },
        };
    }

    void loadFriendlyTextures(eng::ResourceLoaderInterface& resourceLoader)
    {
        uint32_t backDown1 = resourceLoader.loadTexture("textures/GGBackDown1.png");
        uint32_t backDown2 = resourceLoader.loadTexture("textures/GGBackDown2.png");
        uint32_t backUp1 = resourceLoader.loadTexture("textures/GGBackUp1.png");
        uint32_t backUp2 = resourceLoader.loadTexture("textures/GGBackUp2.png");
        uint32_t frontDown1 = resourceLoader.loadTexture("textures/GGFrontDown1.png");
        uint32_t frontDown2 = resourceLoader.loadTexture("textures/GGFrontDown2.png");
        uint32_t frontUp1 = resourceLoader.loadTexture("textures/GGFrontUp1.png");
        uint32_t frontUp2 = resourceLoader.loadTexture("textures/GGFrontUp2.png");
        uint32_t frontUp2Blink = resourceLoader.loadTexture("textures/GGFrontUp2Blink.png");
        uint32_t sideDown1 = resourceLoader.loadTexture("textures/GGSideDown1.png");
        uint32_t sideDown2 = resourceLoader.loadTexture("textures/GGSideDown2.png");
        uint32_t sideUp1 = resourceLoader.loadTexture("textures/GGSideUp1.png");
        uint32_t sideUp2 = resourceLoader.loadTexture("textures/GGSideUp2.png");
        uint32_t sideUp2Blink = resourceLoader.loadTexture("textures/GGSideUp2Blink.png");

        textures.friendly = {
            .front = {
                frontDown1,
                frontDown2,
                frontDown2,
                frontUp1,
                frontUp2,
                frontUp2,
                frontDown1,
                frontDown2,
                frontDown2,
                frontUp1,
                frontUp2Blink,
                frontUp2,
            },
            .back = {
                backDown1,
                backDown2,
                backDown2,
                backUp1,
                backUp2,
                backUp2,
            },
            .side = {
                sideDown1,
                sideDown2,
                sideDown2,
                sideUp1,
                sideUp2,
                sideUp2,
                sideDown1,
                sideDown2,
                sideDown2,
                sideUp1,
                sideUp2Blink,
                sideUp2,
            },
        };
    }

    void loadLeaderTextures(eng::ResourceLoaderInterface& resourceLoader)
    {
        uint32_t backDown1 = resourceLoader.loadTexture("textures/BBBackDown1.png");
        uint32_t backDown2 = resourceLoader.loadTexture("textures/BBBackDown2.png");
        uint32_t backUp1 = resourceLoader.loadTexture("textures/BBBackUp1.png");
        uint32_t backUp2 = resourceLoader.loadTexture("textures/BBBackUp2.png");
        uint32_t frontDown1 = resourceLoader.loadTexture("textures/BBFrontDown1.png");
        uint32_t frontDown2 = resourceLoader.loadTexture("textures/BBFrontDown2.png");
        uint32_t frontUp1 = resourceLoader.loadTexture("textures/BBFrontUp1.png");
        uint32_t frontUp2 = resourceLoader.loadTexture("textures/BBFrontUp2.png");
        uint32_t frontUp2Blink = resourceLoader.loadTexture("textures/BBFrontUp2Blink.png");
        uint32_t sideDown1 = resourceLoader.loadTexture("textures/BBSideDown1.png");
        uint32_t sideDown2 = resourceLoader.loadTexture("textures/BBSideDown2.png");
        uint32_t sideUp1 = resourceLoader.loadTexture("textures/BBSideUp1.png");
        uint32_t sideUp2 = resourceLoader.loadTexture("textures/BBSideUp2.png");
        uint32_t sideUp2Blink = resourceLoader.loadTexture("textures/BBSideUp2Blink.png");

        textures.leader = {
            .front = {
                frontDown1,
                frontDown2,
                frontDown2,
                frontUp1,
                frontUp2,
                frontUp2,
                frontDown1,
                frontDown2,
                frontDown2,
                frontUp1,
                frontUp2Blink,
                frontUp2,
            },
            .back = {
                backDown1,
                backDown2,
                backDown2,
                backUp1,
                backUp2,
                backUp2,
            },
            .side = {
                sideDown1,
                sideDown2,
                sideDown2,
                sideUp1,
                sideUp2,
                sideUp2,
                sideDown1,
                sideDown2,
                sideDown2,
                sideUp1,
                sideUp2Blink,
                sideUp2,
            },
        };
    }

    void loadLevel(uint32_t index)
    {
        const Map& map = maps[index];
        componentArrays.clear();
        freeEntities.clear();
        playerEntities.clear();
        inputQueue.clear();
        inputSpriteEntities.clear();
        gubgubCounterText = Entity::Invalid;
        entityIndexCounter = 0;

        cells.clear();
        cells.resize(map.rows.size());
        std::map<char, std::vector<std::pair<uint32_t, uint32_t>>> markers;
        for (uint32_t row = 0; row < map.rows.size(); ++row)
        {
            cells[row].resize(map.rows[row].size());
            for (uint32_t col = 0; col < map.rows[row].size(); ++col)
            {
                cells[row][col] = Cell { .x = col, .y = row, };
                switch (map.rows[row][col])
                {
                    case 'X':
                        cells[row][col].solid = true;
                        break;
                    case '_':
                        break;
                    default:
                        markers[map.rows[row][col]].push_back({ col, row });
                        break;
                }
            }
        }

        if (auto it = markers.find('P'); it != markers.end() && !it->second.empty())
        {
            auto&& [x, y] = it->second.front();
            initPlayer({ {x, y} });
            mapViewCenter = { x + 0.5, maxTilesVertical - y - 0.5 };
            prevMapViewCenter = mapViewCenter;
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
                auto id = createEntity();
                component<MapCoords>().add(id);
                component<PatrolPoint>().add(id);
                moveEntity(id, x, y);
            }
        }

        if (auto it = markers.find('D'); it != markers.end())
        {
            for (auto&& [x, y] : it->second)
            {
                auto id = createEntity();
                component<Door>().add(id);
                component<Solid>().add(id);
                component<Sprite>().add(id) = {
                    .textureIndex = textures.blank,
                    .color = { 1, 0, 0, 1 },
                };
                component<MapCoords>().add(id);
                moveEntity(id, x, y);
            }
        }

        gubgubCounterText = createEntity();
        component<Text>().add(gubgubCounterText) = Text{
            .text = "GubGubs",
            .scale = { 0.75, 0.75 },
            .background = { 48.0/255.0, 56.0/255.0, 67.0/255.0, 0.8 },
            .foreground = { 164.0/255.0, 197.0/255.0, 175.0/255.0, 1 },
            .position = { 0.5, 0.5 },
        };

        for (uint32_t i = 0; i < map.levelText.size(); ++i)
        {
            component<Text>().add(createEntity()) = {
                .text = map.levelText[i],
                .scale = { 0.5, 0.5 },
                .background = { 48.0/255.0, 56.0/255.0, 67.0/255.0, 0.8 },
                .foreground = { 164.0/255.0, 197.0/255.0, 175.0/255.0, 1 },
                .position = { 1, static_cast<float>(maxTilesVertical - (map.levelText.size() - i + 1) * 0.5f) },
            };
        }

        entitiesNeeded = map.entitiesNeeded;
        currentLevel = index;

        component<MapCoords>().forEach([&](MapCoords& mapCoords, uint32_t id)
        {
            if (component<Sprite>().has(id))
            {
                auto& sprite = component<Sprite>().get(id);
                sprite.x = mapCoords.x;
                sprite.y = mapCoords.y;
            }
        });
    }

    void init(eng::ResourceLoaderInterface& resourceLoader, eng::SceneInterface& scene, eng::InputInterface& input) override
    {
        textures.blank = resourceLoader.loadTexture("textures/blank.png");
        textures.sightline = {
            resourceLoader.loadTexture("textures/LOS1.png"),
            resourceLoader.loadTexture("textures/LOS2.png"),
            resourceLoader.loadTexture("textures/LOS3.png"),
            resourceLoader.loadTexture("textures/LOS4.png"),
            resourceLoader.loadTexture("textures/LOS5.png"),
            resourceLoader.loadTexture("textures/LOS6.png"),
        };
        textures.sightlineEnd = {
            resourceLoader.loadTexture("textures/LOSHalf1.png"),
            resourceLoader.loadTexture("textures/LOSHalf2.png"),
            resourceLoader.loadTexture("textures/LOSHalf3.png"),
            resourceLoader.loadTexture("textures/LOSHalf4.png"),
            resourceLoader.loadTexture("textures/LOSHalf5.png"),
            resourceLoader.loadTexture("textures/LOSHalf6.png"),
        };
        textures.zap = {
            resourceLoader.loadTexture("textures/Zap1.png"),
            resourceLoader.loadTexture("textures/Zap2.png"),
            resourceLoader.loadTexture("textures/Zap3.png"),
            resourceLoader.loadTexture("textures/Zap4.png"),
            resourceLoader.loadTexture("textures/Zap5.png"),
            resourceLoader.loadTexture("textures/Zap6.png"),
        };
        textures.zapHit = {
            resourceLoader.loadTexture("textures/ZapHit1.png"),
            resourceLoader.loadTexture("textures/ZapHit2.png"),
            resourceLoader.loadTexture("textures/ZapHit3.png"),
            resourceLoader.loadTexture("textures/ZapHit4.png"),
            resourceLoader.loadTexture("textures/ZapHit5.png"),
            resourceLoader.loadTexture("textures/ZapHit6.png"),
        };
        textures.bonk = {
            resourceLoader.loadTexture("textures/Bonk1.png"),
            resourceLoader.loadTexture("textures/Bonk2.png"),
            resourceLoader.loadTexture("textures/Bonk3.png"),
            resourceLoader.loadTexture("textures/Bonk4.png"),
            resourceLoader.loadTexture("textures/Bonk5.png"),
            resourceLoader.loadTexture("textures/Bonk6.png"),
        };
        textures.enemySleepy = {
            resourceLoader.loadTexture("textures/NNSleepy1.png"),
            resourceLoader.loadTexture("textures/NNSleepy2.png"),
            resourceLoader.loadTexture("textures/NNSleepy3.png"),
            resourceLoader.loadTexture("textures/NNSleepy4.png"),
            resourceLoader.loadTexture("textures/NNSleepy5.png"),
            resourceLoader.loadTexture("textures/NNSleepy6.png"),
        };
        textures.friendlySleepy = {
            resourceLoader.loadTexture("textures/GGSleepy1.png"),
            resourceLoader.loadTexture("textures/GGSleepy2.png"),
            resourceLoader.loadTexture("textures/GGSleepy3.png"),
            resourceLoader.loadTexture("textures/GGSleepy4.png"),
            resourceLoader.loadTexture("textures/GGSleepy5.png"),
            resourceLoader.loadTexture("textures/GGSleepy6.png"),
        };
        textures.transform = {
            resourceLoader.loadTexture("textures/Transform1.png"),
            resourceLoader.loadTexture("textures/Transform2.png"),
            resourceLoader.loadTexture("textures/Transform3.png"),
            resourceLoader.loadTexture("textures/Transform4.png"),
            resourceLoader.loadTexture("textures/Transform5.png"),
            resourceLoader.loadTexture("textures/Transform6.png"),
        };
        textures.arrow = resourceLoader.loadTexture("textures/arrow.png");
        textures.font = resourceLoader.loadTexture("textures/font.png");
        textures.wall = resourceLoader.loadTexture("textures/WallObstacle.png");
        textures.floor = resourceLoader.loadTexture("textures/FloorTile.png");
        loadEnemyTextures(resourceLoader);
        loadFriendlyTextures(resourceLoader);
        loadLeaderTextures(resourceLoader);

        directionInputMappings[Direction::Up] = input.createMapping();
        directionInputMappings[Direction::Left] = input.createMapping();
        directionInputMappings[Direction::Down] = input.createMapping();
        directionInputMappings[Direction::Right] = input.createMapping();

        input.mapKey(directionInputMappings[Direction::Up], glfwGetKeyScancode(GLFW_KEY_W));
        input.mapKey(directionInputMappings[Direction::Left], glfwGetKeyScancode(GLFW_KEY_A));
        input.mapKey(directionInputMappings[Direction::Down], glfwGetKeyScancode(GLFW_KEY_S));
        input.mapKey(directionInputMappings[Direction::Right], glfwGetKeyScancode(GLFW_KEY_D));

        maps = {
            Map {
                .rows = {
                    "XXXXXDXXXXX"sv,
                    "X_________X"sv,
                    "X_________X"sv,
                    "X_________X"sv,
                    "X____P____X"sv,
                    "X_________X"sv,
                    "X_________X"sv,
                    "XXXXXXXXXXX"sv,
                },
                .entitiesNeeded = 1,
                .levelText = {
                    "YOU ARE THE LEADER OF THE PEACEFUL GUBGUBS",
                    "BUT A HORRIBLE MIND VIRUS HAS INFECTED YOUR FELLOWS...",
                    "REACH THE DOOR TO COMPLETE LEVEL",
                },
            },
            Map {
                .rows = {
                    "XXXXXDXXXXX"sv,
                    "X_________X"sv,
                    "X_E_______X"sv,
                    "X_________X"sv,
                    "X____P____X"sv,
                    "X_________X"sv,
                    "X_________X"sv,
                    "XXXXXXXXXXX"sv,
                },
                .entitiesNeeded = 2,
                .levelText = {
                    "INFECTED GUBGUBS ARE BRUTISH AND AGGRESSIVE",
                    "THEY CAN'T BE REASONED WITH, BUT CAN BE STUNNED BY A HEAD ON COLLISION",
                    "BEFRIEND STUNNED GUBGUBS BY WALKING OVER THEM",
                },
            },
            Map {
                .rows = {
                    "XXXXXDXXXXX"sv,
                    "X_________X"sv,
                    "X_TT___TT_X"sv,
                    "X_E_____E_X"sv,
                    "X____P____X"sv,
                    "X_TT___TT_X"sv,
                    "X_________X"sv,
                    "XXXXXXXXXXX"sv,
                },
                .entitiesNeeded = 3,
                .levelText = {
                    "IF AN INFECTED SPOTS YOU OR YOUR FOLLOWERS IT WILL SHOOT ITS STUN BEAM",
                    "ANY FOLLOWING GUBGUB IN THE TRAIN WILL BE STUNNED,",
                    "BUT THE LEADER IS NOT AFFECTED",
                },
            },
            Map {
                .rows = {
                    "XXXXXXXXXXXXXXXXXXXX"sv,
                    "XT______T_T_______TX"sv,
                    "X________XE________X"sv,
                    "X__T_T__T_T________X"sv,
                    "X___XE________T_T__X"sv,
                    "X__T_T_________XE__X"sv,
                    "X_____________T_T__X"sv,
                    "X________P_________X"sv,
                    "X__________________D"sv,
                    "X__________________X"sv,
                    "XT________________TX"sv,
                    "XXXXXXXXXXXXXXXXXXXX"sv,
                },
                .entitiesNeeded = 4,
            },
            Map {
                .rows = {
                    "XXXXXXXXXXXXXXXXXXXX"sv,
                    "X_T_______E______T_X"sv,
                    "X__XXXXXXXXXXXXXX__X"sv,
                    "X__X________E____T_X"sv,
                    "X__XT___________T__X"sv,
                    "X__X_XXXXXXXXXXX___X"sv,
                    "X_EXT_____TXT___T__X"sv,
                    "X__X_____P_X_______X"sv,
                    "X__X_______X__E____D"sv,
                    "X__X_______X_______X"sv,
                    "X_T_T_____T_T______X"sv,
                    "XXXXXXXXXXXXXXXXXXXX"sv,
                },
                .entitiesNeeded = 5,
            },
        };

        loadLevel(0);
    }

    void gameTick()
    {
        while (!component<Transient>().entities.empty())
        {
            destroyEntity(component<Transient>().entities.front());
        }

        component<Sprite>().forEach([&](Sprite& sprite, uint32_t id)
        {
            sprite.prevx = sprite.x;
            sprite.prevy = sprite.y;
        });

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
            auto& sprite = component<Sprite>().get(inputSpriteEntities.front());
            sprite.y -= 1;

            for (uint32_t i = 1; i < inputSpriteEntities.size(); ++i)
            {
                auto id = inputSpriteEntities[i];
                auto& sprite = component<Sprite>().get(id);
                sprite.x += 1;
            }

            if (!playerEntities.empty())
            {
                component<CharacterAnimator>().get(playerEntities.front()).direction = event.direction;
                const auto& coords = component<MapCoords>().get(playerEntities.front());

                auto [dx, dy] = directionCoords(event.direction);
                clampDeltaToMap(coords.x, coords.y, dx, dy);

                const auto& cell = cells[coords.y + dy][coords.x + dx];
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
                            component<Neutral>().add(target).wasFriendly = false;
                            component<CharacterAnimator>().remove(target);
                            component<SequenceAnimator>().get(target).sequence = &textures.enemySleepy;

                            uint32_t bonker = createEntity();
                            component<MapCoords>().add(bonker);
                            component<Sprite>().add(bonker);
                            component<SequenceAnimator>().add(bonker).sequence = &textures.bonk;
                            component<Transient>().add(bonker);
                            moveEntity(bonker, coords.x + dx, coords.y + dy);
                        }
                        else
                        {
                            if (capture)
                            {
                                component<Neutral>().remove(target);
                                component<Friendly>().add(target);
                                component<CharacterAnimator>().add(target) = {
                                    .textureSet = &textures.friendly,
                                };
                                playerEntities.push_back(target);
                            }

                            for (uint32_t i = playerEntities.size() - 1; i > 0; --i)
                            {
                                const auto& nextCoords = component<MapCoords>().get(playerEntities[i - 1]);
                                moveEntity(playerEntities[i], nextCoords.x, nextCoords.y);
                            }

                            moveEntity(playerEntities.front(), coords.x + dx, coords.y + dy);
                            const auto& cell = cells[coords.y][coords.x];
                            if (auto it = std::find_if(cell.occupants.begin(), cell.occupants.end(),
                                    [&](auto oid){ return component<Door>().has(oid); });
                                it != cell.occupants.end())
                            {
                                if (component<Door>().get(*it).open)
                                {
                                    if (currentLevel + 1 < maps.size())
                                    {
                                        loadLevel(currentLevel + 1);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        for (uint32_t i = 1; i < playerEntities.size(); ++i)
        {
            const auto& coords = component<MapCoords>().get(playerEntities[i]);
            const auto& nextCoords = component<MapCoords>().get(playerEntities[i - 1]);
            component<CharacterAnimator>().get(playerEntities[i]).direction = directionFromDelta((int)nextCoords.x - (int)coords.x, (int)nextCoords.y - (int)coords.y);
        }

        component<Text>().get(gubgubCounterText).text = "GubGubs: " + std::to_string(playerEntities.size()) + " / " + std::to_string(entitiesNeeded);
        component<Door>().forEach([&](Door& door, uint32_t id)
        {
            const bool open = playerEntities.size() == entitiesNeeded;
            if (open != door.open)
            {
                door.open = open;
                if (open)
                {
                    component<Solid>().remove(id);
                    component<Sprite>().get(id).color = { 0, 1, 0, 1 };
                }
                else
                {
                    component<Solid>().add(id);
                    component<Sprite>().get(id).color = { 1, 0, 0, 1 };
                }
            }
        });

        component<Neutral>().forEach([this](Neutral& neutral, uint32_t id)
        {
            if (neutral.cooldown == 0)
            {
                component<Enemy>().add(id);
                component<CharacterAnimator>().add(id) = {
                    .textureSet = &textures.enemy,
                };
                component<Neutral>().removeLater(id);
            }
            else
            {
                --neutral.cooldown;
                if (neutral.cooldown == 0 && neutral.wasFriendly)
                {
                    component<SequenceAnimator>().get(id).sequence = &textures.transform;
                }
            }
        });

        component<Enemy>().forEach([this](Enemy& enemy, uint32_t id)
        {
            enemyLogic(enemy, id);
        });

        component<MapCoords>().forEach([&](MapCoords& mapCoords, uint32_t id)
        {
            if (component<Sprite>().has(id))
            {
                auto& sprite = component<Sprite>().get(id);
                sprite.x = mapCoords.x;
                sprite.y = mapCoords.y;
            }
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
                uint32_t id = createEntity();
                component<Sprite>().add(id) = Sprite{
                    .textureIndex = textures.arrow,
                    .color = { 1, 1, 0, 1 },
                    .direction = direction,
                    .x = maxTilesHorizontal - 1 - offset,
                    .y = maxTilesVertical - 1,
                };
                component<InputIcon>().add(id);
                inputSpriteEntities.push_back(id);
            }
        }

        if (animationFrameTimer >= animationFrameInterval)
        {
            component<Enemy>().forEach([&](Enemy& enemy, uint32_t id)
            {
                ++enemy.lineFrame;
            });

            component<SequenceAnimator>().forEach([&](SequenceAnimator& animator, uint32_t id)
            {
                ++animator.frame;
            });

            animationFrameTimer -= animationFrameInterval;
        }
        animationFrameTimer += deltaTime;

        if (tickTimer >= tickInterval)
        {
            prevMapViewCenter = mapViewCenter;
            if (!playerEntities.empty())
            {
                const auto& coords = component<MapCoords>().get(playerEntities.front());
                mapViewCenter = glm::vec2(coords.x + 0.5, maxTilesVertical - coords.y - 0.5);
            }

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

        glm::vec2 mapViewCenterOffset = glm::mix(prevMapViewCenter, mapViewCenter, tween) - glm::vec2(0.5f * maxTilesHorizontal, 0.5f * maxTilesVertical);

        scene.instances().clear();
        scene.instances().push_back(eng::Instance {
                    .position = glm::vec2(0.5 * cells.front().size(), maxTilesVertical - 0.5 * cells.size()) - mapViewCenterOffset,
                    .scale = glm::vec2(cells.front().size(), cells.size()),
                    .texCoordScale = glm::vec2(cells.front().size(), cells.size()),
                    .textureIndex = textures.floor,
                });
        for (uint32_t i = 0; i < cells.size(); ++i)
        {
            for (uint32_t j = 0; j < cells[i].size(); ++j)
            {
                if (cells[i][j].solid)
                {
                    scene.instances().push_back(eng::Instance {
                                .position = glm::vec2(j + 0.5, maxTilesVertical - i - 0.5) - mapViewCenterOffset,
                                .textureIndex = textures.wall,
                            });
                }
            }
        }

        component<CharacterAnimator>().forEach([&](CharacterAnimator& animator, uint32_t id)
        {
            if (animator.textureSet)
            {
                auto& sequenceAnimator = component<SequenceAnimator>().get(id);
                sequenceAnimator.sequence = &(animator.direction == Direction::Up ? animator.textureSet->back
                    : animator.direction == Direction::Down ? animator.textureSet->front : animator.textureSet->side);

                auto& sprite = component<Sprite>().get(id);
                sprite.flipHorizontal = animator.direction == Direction::Right;
            }
        });

        component<SequenceAnimator>().forEach([&](SequenceAnimator& animator, uint32_t id)
        {
            if (animator.sequence)
            {
                if (animator.frame >= animator.sequence->size())
                {
                    animator.frame = 0;
                }

                auto& sprite = component<Sprite>().get(id);
                sprite.textureIndex = animator.sequence->at(animator.frame);
            }
        });

        component<Sprite>().forEach([&](const Sprite& sprite, uint32_t id)
        {
            glm::vec2 position(sprite.x + 0.5, maxTilesVertical - sprite.y - 0.5);
            if (tween < 1.0f && sprite.prevx != std::numeric_limits<uint32_t>::max() && sprite.prevy != std::numeric_limits<uint32_t>::max()
                    && (sprite.prevx != sprite.x || sprite.prevy != sprite.y))
            {
                position = glm::mix(glm::vec2(sprite.prevx + 0.5, maxTilesVertical - sprite.prevy - 0.5), position, tween);
            }
            if (component<MapCoords>().has(id))
            {
                position -= mapViewCenterOffset;
            }
            scene.instances().push_back(eng::Instance {
                        .position = position,
                        .minTexCoord = { sprite.flipHorizontal ? 1 : 0, 0 },
                        .texCoordScale = { sprite.flipHorizontal ? -1 : 1, 1 },
                        .angle = directionAngle(sprite.direction),
                        .textureIndex = sprite.textureIndex,
                        .tintColor = sprite.color,
                    });
        });

        component<Enemy>().forEach([&](Enemy& enemy, uint32_t id)
        {
            const auto& mapCoords = component<MapCoords>().get(id);

            uint32_t textureIndex;
            uint32_t endTextureIndex;
            glm::vec4 tintColor = { 1, 1, 1, 1 };
            if (enemy.state == Enemy::State::Attack)
            {
                if (enemy.lineFrame >= textures.zap.size())
                {
                    enemy.lineFrame = 0;
                }
                textureIndex = textures.zap[enemy.lineFrame];
                endTextureIndex = textures.zapHit[enemy.lineFrame];
            }
            else
            {
                if (enemy.lineFrame >= textures.sightline.size())
                {
                    enemy.lineFrame = 0;
                }
                textureIndex = textures.sightline[enemy.lineFrame];
                endTextureIndex = textures.sightlineEnd[enemy.lineFrame];
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

            scan(mapCoords.x, mapCoords.y, enemy.facingDirection, 0,
                [&](const Cell& cell, uint32_t distance)
                {
                    if (auto it = std::find_if(cell.occupants.begin(), cell.occupants.end(),
                            [&](auto oid){ return component<Solid>().has(oid); });
                        it != cell.occupants.end())
                    {
                        if (component<Friendly>().has(*it) || component<Neutral>().has(*it))
                        {
                            scene.instances().push_back(eng::Instance {
                                        .position = glm::vec2(cell.x + 0.5, maxTilesVertical - cell.y - 0.5) - mapViewCenterOffset,
                                        .angle = angle,
                                        .textureIndex = endTextureIndex,
                                        .tintColor = tintColor,
                                    });
                        }
                        return true;
                    }
                    scene.instances().push_back(eng::Instance {
                                .position = glm::vec2(cell.x + 0.5, maxTilesVertical - cell.y - 0.5) - mapViewCenterOffset,
                                .angle = angle,
                                .textureIndex = textureIndex,
                                .tintColor = tintColor,
                            });
                    return false;
                });
        });

        component<Text>().forEach([&](const Text& text, uint32_t id)
        {
            constexpr glm::vec2 texCoordScale = { 1.0f / 16.0f, 1.0f / 8.0f };
            scene.instances().push_back(eng::Instance {
                    .position = { text.position.x + 0.25f * text.text.size() * text.scale.x, maxTilesVertical - text.position.y - 0.5 * text.scale.y },
                    .scale = { 0.5f * text.text.size() * text.scale.x, text.scale.y },
                    .textureIndex = textures.blank,
                    .tintColor = text.background,
                });
            for (uint32_t i = 0; i < text.text.size(); ++i)
            {
                glm::vec2 minTexCoord = glm::vec2(text.text[i] / 8, text.text[i] % 8) * texCoordScale;
                scene.instances().push_back(eng::Instance {
                        .position = { text.position.x + (i + 0.5) * 0.5 * text.scale.x, maxTilesVertical - text.position.y - 0.5 * text.scale.y },
                        .scale = { text.scale.x * 0.5, text.scale.y },
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
            .windowWidth = 2 * GameLogic::texelsPerTile * GameLogic::maxTilesHorizontal,
            .windowHeight = 2 * GameLogic::texelsPerTile * GameLogic::maxTilesVertical,
        });
}
