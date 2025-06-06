#include "s7.hpp"
#include "s7lib.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <boost/concept_check.hpp>

using namespace s7;
int main(int argc, char *argv[]){


    std::cout << "Hello S7" << '\n';
    DataVariant d1(1.2);
    auto cp = std::make_shared<Constant>(d1);
    s7::Schema s;
    s.insert(std::move(cp));
    DataVariant is1(0.0);
    auto ig = std::make_shared<Integrator>(is1);
    s.insert(ig);

    std::cout << cp->outputs[0]->value() << '\n';
    return EXIT_SUCCESS;
}