#pragma once
#include <memory>
#include <string>

#include "s7value.hpp"

namespace s7 {

    class Element;

    class Pin {
        public:
        virtual ~Pin() = default;
        explicit Pin() = default;
        Element *owner = nullptr;
        std::string name;
    };

    class Output : public Pin {
    public:
       explicit Output(UniValue& val_ref) : val(val_ref) {};
       UniValue& val;
       [[nodiscard]] UniValue value() const {return val;};
    };

    class Input : public Pin {
        public:
        Output *connected = nullptr;
        virtual UniValue value() = 0;
    };

    class Schema;

    class Element {
    public:
        virtual ~Element() = default;
        Schema *owner = nullptr;
        std::vector<std::unique_ptr<Input>> inputs;
        std::vector<std::unique_ptr<Output>> outputs;
        virtual void tick() = 0;
    };

    // no inputs
    class Source : public Element {

    };

    class Constant : public Source
    {
    public:
        explicit Constant(const UniValue& val)
        {
            constant = val;
            outputs.push_back(std::make_unique<Output>(constant));
        }
        void tick() override{};
        UniValue constant;
    };

    //no outputs
    class Sink : public Element
    {

    };

    //collection of interconnected elements
    class Schema : public Element
    {
        public:
        std::vector<std::unique_ptr<Element> >elements;
        void tick() override{};
    };

}
