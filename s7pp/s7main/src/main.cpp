#include "s7.hpp"
#include "s7lib.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>

using namespace s7;
int main(int argc, char *argv[]){


    std::cout << "Hello S7" << '\n';
    DataVariant d1(1.2);
    s7::Constant c(d1);
    s7::Schema s;

    std::cout << c.outputs[0]->value() << '\n';
    return EXIT_SUCCESS;
}