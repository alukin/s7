#pragma once
#include <memory>
#include <string>
#include <vector>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
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
        Output *connected_to = nullptr;
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
    };

    class Schema;

    class Element {
    public:
        Element()
        {
            id_ = boost::uuids::random_generator()();
        }
        virtual ~Element() = default;
        Schema *owner = nullptr;
        std::vector<std::unique_ptr<Input>> inputs;
        std::vector<std::unique_ptr<Output>> outputs;
        virtual void tick() = 0;
        [[nodiscard]] bool isSource() const { return inputs.empty(); }
        [[nodiscard]] bool isSink() const { return outputs.empty(); }
        [[nodiscard]] boost::uuids::uuid getId() const { return id_; }
    private:
        boost::uuids::uuid id_{};
    };

    typedef std::shared_ptr<Element> ElementSPtr;
    typedef std::unique_ptr<Element> ElementPtr;

    //collection of interconnected elements
    class Schema : public Element
    {
        public:
        void tick() override{};
        void insert(ElementPtr e);
        bool connect(uuid el1, std::string output_id, uuid el2, std::string input_id );
        private:
        std::unordered_map<std::string,ElementPtr> elements;

    };

}
