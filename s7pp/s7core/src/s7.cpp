#include "s7.hpp"
#include <boost/lexical_cast.hpp>

namespace s7 {

void Schema::insert(ElementPtr e)
{
  const auto hash = boost::lexical_cast<std::string>(e->getId());
  elements[hash] = std::move(e);
}

bool Schema::connect(uuid el1, std::string output_id, uuid el2, std::string input_id)
{
  bool res = false;
  return res;
}
}
