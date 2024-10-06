#include "engine.hpp"

#include <map>

struct GLFWwindow;

namespace eng
{
    class InputManager : public InputInterface
    {
        enum class InputType
        {
            None,
            Key,
            MouseButton,
            Cursor,
        };

        struct Input
        {
            InputType inputType = InputType::None;
            int code;

            union
            {
                bool boolean;
                double real;
            } state, previousState;

            int mappingCount = 0;
        };

        struct Mapping
        {
            uint32_t inputIndex;
        };

    public:
        explicit InputManager();

        uint32_t createMapping() override;
        void mapKey(const uint32_t mapping, const int scancode) override;
        void mapMouseButton(const uint32_t mapping, const int scancode) override;
        void mapCursor(const uint32_t mapping, const CursorAxis axis) override;
        bool getBoolean(const uint32_t mapping, const BoolStateEvent event) const override;
        double getReal(const uint32_t mapping, const RealStateEvent event) const override;

        void nextFrame();

        void handleKey(int key, int scancode, int action, int mods);
        void handleMouseButton(int button, int action, int mods);
        void handleCursorPosition(double x, double y);

    private:
        void map(const uint32_t mapping, const uint32_t inputIndex);
        void unmap(const uint32_t mapping);
        uint32_t getInputIndex(std::map<int, uint32_t>& map, const int code, Input&& input);
        void updateStateBoolean(const std::map<int, uint32_t>& map, const int code, const bool state);
        void updateStateReal(const std::map<int, uint32_t>& map, const int code, const double state);

        std::vector<Input> inputs;
        std::vector<Mapping> mappings;
        std::map<int, uint32_t> keyMap;
        std::map<int, uint32_t> mouseButtonMap;
        std::map<int, uint32_t> cursorMap;
        std::vector<uint32_t> freeInputs;
    };
}
