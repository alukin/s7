#pragma once
namespace s7 {
    class Pin {
        public:
        explicit Pin();
    };
    class Input {

    };
    class Output {

    };
    class Element {
    public:
        void tick();
    };
    class Generator : public Element {

    };
}
