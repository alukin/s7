#pragma once
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <unordered_map>
#include "s7datavariant.hpp"

namespace s7 {

using namespace boost::uuids;

    class Element;

    class Pin {
        public:
        virtual ~Pin() = default;
        explicit Pin() = default;
        Element *owner = nullptr;
        std::string name;
        std::string description;
    };


    class Output : public Pin {
    public:
       explicit Output(DataVariant& value_ref) : val(value_ref) {};
       DataVariant& val;
       [[nodiscard]] DataVariant& value() const {return val;};
    };

    class Input : public Pin {
        public:
        bool connect( std::shared_ptr<Output> o)
        {
            //TODO: check IO compatibility
            connected_to = o;
            return true;
        }

        [[nodiscard]] DataVariant& value() const
        {
            if (connected_to != nullptr)
            {
                return connected_to->value();
            } else
            {
               throw std::runtime_error("Input is not connected");
            }
        };
    private:
        std::shared_ptr<Output> connected_to = nullptr;
    };

    class IdGEnerator
    {
        public:
        std::string operator()() const
        {
            auto uuid = boost::uuids::random_generator()();
            std::stringstream ss;
            ss << uuid;
            return ss.str();
        }
    };

    class Schema;

    class Element {
    public:
        Element()
        {
            id_ = IdGEnerator()();
        }
        explicit Element(std::string& id) : id_(id) {};
        virtual ~Element() = default;
        Schema *owner = nullptr;
        std::vector<std::shared_ptr<Input>> inputs;
        std::vector<std::shared_ptr<Output>> outputs;
        virtual void tick() = 0;
        [[nodiscard]] bool isSource() const { return inputs.empty(); }
        [[nodiscard]] bool isSink() const { return outputs.empty(); }
        [[nodiscard]] std::string getId() const { return id_; }
        std::shared_ptr<Input> getInput(int idx){ return inputs.at(idx); }
        std::shared_ptr<Output> getOutput(int idx){ return outputs.at(idx); }
    private:
        std::string id_;
    };

    typedef std::shared_ptr<Element> ElementPtr;

    //collection of interconnected elements
    class Schema : public Element
    {
        public:
        void tick() override{};
        void insert(ElementPtr e);
        bool connect(ElementPtr el1, int output_idx, ElementPtr el2, int input_idx );
        private:
        std::unordered_map<std::string,ElementPtr> elements;

    };

}
