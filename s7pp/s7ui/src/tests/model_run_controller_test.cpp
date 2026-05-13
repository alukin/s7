#include "model_run_controller.hpp"

#include <cassert>

int main()
{
    using s7ui::ui::ModelRunController;

    assert(ModelRunController::computeTicks(0, 1, 0, 0.001) == 60000);
    assert(ModelRunController::computeTicks(0, 0, 0, 0.001) == 1000);
    assert(ModelRunController::computeTicks(1, 0, 0, 0.01) == 360000);
    assert(ModelRunController::computeTicks(0, 0, 1, 1000.0) == 1);

    return 0;
}

