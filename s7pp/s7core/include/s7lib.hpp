//
// Created by al on 6/1/25.
//

#pragma once
#include <utility>

#include "i_s7lib.hpp"
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
        explicit Integrator(DataVariant  init_state) : state(std::move(init_state))
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
    //use array of weights to set num3r of inputs
    };

//voltage controlled amplifier
    class Vca :public Element
    {
    };

    class HarmonicOscillator :public Element
    {

    };

    class s7lib : public Is7lib {
//this is the utility class to load the library dynamically
    };

}
