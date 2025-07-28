#include "s7.hpp"
#include "s7lib.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <boost/concept_check.hpp>

using namespace s7;
int main(int argc, char *argv[]){


    std::cout << "Hello S7" << '\n';
    auto s = std::make_shared<Schema>(1e-9);

    DataVariant d1(1.2);
    auto c = std::make_shared<Constant>(d1);
    s->insert(c);

    DataVariant is1(0.0);
    auto ig = std::make_shared<Integrator>(is1);
    s->insert(ig);

    s->connect(c,0,ig,0);
    s->run(10);
    std::cout << c->outputs[0]->value() << '\n';
    return EXIT_SUCCESS;
}