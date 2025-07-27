//
// Created by al on 6/1/25.
//

#pragma once
#include <utility>

#include "s7.hpp"

namespace s7 {
    
    class Constant : public Element
    {
    public:
        ~Constant() override = default;
        Constant() : Constant("",DataVariant(1.0)) {};
        explicit Constant(const DataVariant& val) : Constant("",val) {};
        explicit Constant( std::string const& id, DataVariant val) :Element(id), constant(std::move(val))
        {
            outputs.push_back(std::make_unique<Output>(constant));
        }
        void tick() override
        {
            //empty
        };
        DataVariant constant;
    };

    class Integrator :public Element{
        public:
        explicit Integrator(DataVariant const& init_state) : state(init_state)
        {
            inputs.push_back(std::make_unique<Input>());
            outputs.push_back(std::make_unique<Output>(state));
        }
        void tick() override
        {
             state = state + inputs[0]->value();
        };
        DataVariant state;
    };

    class Sum :public Element {

    };

    class s7lib {

    };
}
