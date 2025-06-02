//
// Created by al on 6/1/25.
//

#pragma once
#include "s7.hpp"

namespace s7 {
    
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

    class Integrator :public Element{

    };

    class Sum :public Element {

    };

    class s7lib {

    };
}
