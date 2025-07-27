#include "s7.hpp"
#include <boost/lexical_cast.hpp>

namespace s7 {

  void Schema::tick()
  {
      //TOD: implement
  }

void Schema::insert(ElementPtr e)
{
  elements[e->getId()] = e;
}

bool Schema::connect(ElementPtr el1, int output_idx, ElementPtr el2, int input_idx)
{
  auto in = el2->getInput(input_idx);
  auto out = el1->getOutput(output_idx);
  in->connect(out);
      el1->owner = shared_from_this();
      el2->owner = shared_from_this();

  //TODO: netlist
  return true;
}

void Schema::run(long ticks)
{
    //run according to order
}

void Schema::run_s(double seconds)
{
    run(lround(seconds/discr_time));
}
}
