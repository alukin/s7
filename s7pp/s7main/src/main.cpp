#include "s7.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>

using s7::operator<<;
int main(int argc, char *argv[]){

    s7::UniValue d2 = std::complex<double>(1.0, -2.5);
    std::cout << "Hello S7" << '\n';
    std::cout << d2 << '\n';

    s7::Constant c(1.0);
    std::cout << c.outputs[0]->value() << '\n';
    return EXIT_SUCCESS;
}