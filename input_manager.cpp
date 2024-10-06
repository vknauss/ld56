#include "input_manager.hpp"

#include <GLFW/glfw3.h>

using eng::InputManager;

void InputManager::handleKey(int key, int scancode, int action, int mods)
{
    updateStateBoolean(keyMap, scancode, action != GLFW_RELEASE);
}

void InputManager::handleMouseButton(int button, int action, int mods)
{
    updateStateBoolean(mouseButtonMap, button, action != GLFW_RELEASE);
}

void InputManager::handleCursorPosition(double x, double y)
{
    updateStateReal(cursorMap, static_cast<int>(CursorAxis::X), x);
    updateStateReal(cursorMap, static_cast<int>(CursorAxis::Y), y);
}

InputManager::InputManager()
{
    inputs.push_back(Input {});
}

uint32_t InputManager::createMapping()
{
    uint32_t index = mappings.size();
    mappings.push_back(Mapping {
            .inputIndex = 0,
        });
    return index;
}

void InputManager::mapKey(const uint32_t mapping, const int scancode)
{
    unmap(mapping);
    map(mapping, getInputIndex(keyMap, scancode, Input {
                .inputType = InputType::Key,
                .code = scancode,
            }));
}

void InputManager::mapMouseButton(const uint32_t mapping, const int scancode)
{
    unmap(mapping);
    map(mapping, getInputIndex(mouseButtonMap, scancode, Input {
                .inputType = InputType::MouseButton,
                .code = scancode,
            }));
}

void InputManager::mapCursor(const uint32_t mapping, const CursorAxis axis)
{
    unmap(mapping);
    map(mapping, getInputIndex(cursorMap, static_cast<int>(axis), Input {
                .inputType = InputType::Cursor,
                .code = static_cast<int>(axis),
            }));
}

bool InputManager::getBoolean(const uint32_t mapping, const BoolStateEvent event) const
{
    const auto& input = inputs[mappings[mapping].inputIndex];
    bool value = false;
    switch (event)
    {
        case BoolStateEvent::Down:
            value = input.state.boolean;
            break;
        case BoolStateEvent::Pressed:
            value = input.state.boolean && !input.previousState.boolean;
            break;
        case BoolStateEvent::Released:
            value = !input.state.boolean && input.previousState.boolean;
            break;
    }
    return value;
}

double InputManager::getReal(const uint32_t mapping, const RealStateEvent event) const
{
    const auto& input = inputs[mappings[mapping].inputIndex];
    double value = 0.0f;
    switch (event)
    {
        case RealStateEvent::Value:
            value = input.state.real;
            break;
        case RealStateEvent::Delta:
            value = input.state.real - input.previousState.real;
            break;
    }
    return value;
}

void InputManager::nextFrame()
{
    for (auto& input : inputs)
    {
        input.previousState = input.state;
    }
}

void InputManager::map(const uint32_t mapping, const uint32_t inputIndex)
{
    mappings[mapping].inputIndex = inputIndex;
    ++inputs[inputIndex].mappingCount;
}

void InputManager::unmap(const uint32_t mapping)
{
    uint32_t inputIndex = mappings[mapping].inputIndex;
    if (inputIndex > 0 && (--inputs[inputIndex].mappingCount) == 0)
    {
        switch (inputs[inputIndex].inputType)
        {
        case InputType::None:
            break;
        case InputType::Key:
            keyMap.erase(inputs[inputIndex].code);
            break;
        case InputType::MouseButton:
            mouseButtonMap.erase(inputs[inputIndex].code);
            break;
        case InputType::Cursor:
            break;
        }
        freeInputs.push_back(inputIndex);
    }
}

uint32_t InputManager::getInputIndex(std::map<int, uint32_t>& map, const int code, Input&& input)
{
    auto it = map.find(code);
    if (it == map.end())
    {
        bool inserted;
        if (freeInputs.empty())
        {
            std::tie(it, inserted) = map.emplace(code, inputs.size());
            inputs.push_back(std::forward<Input>(input));
        }
        else
        {
            uint32_t index = freeInputs.back();
            freeInputs.pop_back();
            std::tie(it, inserted) = map.emplace(code, index);
            inputs[index] = std::forward<Input>(input);
        }
    }
    return it->second;
}

void InputManager::updateStateBoolean(const std::map<int, uint32_t>& map, const int code, const bool state)
{
    if (auto it = map.find(code); it != map.end())
    {
        inputs[it->second].state.boolean = state;
    }
}

void InputManager::updateStateReal(const std::map<int, uint32_t>& map, const int code, const double state)
{
    if (auto it = map.find(code); it != map.end())
    {
        inputs[it->second].state.real = state;
    }
}
