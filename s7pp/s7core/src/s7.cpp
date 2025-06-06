#include "s7.hpp"
#include <boost/lexical_cast.hpp>

namespace s7 {

void Schema::insert(ElementPtr e)
{
  elements[e->getId()] = std::move(e);
}

bool Schema::connect(ElementPtr el1, int output_idx, ElementPtr el2, int input_idx)
{
  auto in = el1->getInput(input_idx);
  auto out = el2->getOutput(output_idx);
  in->connect(out);
  //TODO: netlist
  return true;
}
}
