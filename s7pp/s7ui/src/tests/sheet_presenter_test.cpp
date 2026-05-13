#include "sheet_presenter.hpp"
#include <cassert>
#include <string>
using Preset = s7ui::ui::SheetPresenter::Preset;
using s7ui::ui::SheetPresenter;
using s7ui::ui::SheetSizeMm;
static void test_sizeForPreset()
{
    const auto a4 = SheetPresenter::sizeForPreset(Preset::A4Landscape);
    assert(a4.width  == 297.0);
    assert(a4.height == 210.0);
    const auto a3 = SheetPresenter::sizeForPreset(Preset::A3Landscape);
    assert(a3.width  == 420.0);
    assert(a3.height == 297.0);
    const auto a2 = SheetPresenter::sizeForPreset(Preset::A2Landscape);
    assert(a2.width  == 594.0);
    const auto a1 = SheetPresenter::sizeForPreset(Preset::A1Landscape);
    assert(a1.width  == 841.0);
}
static void test_standardName()
{
    assert(std::string(SheetPresenter::standardName(Preset::A4Landscape)) == "A4");
    assert(std::string(SheetPresenter::standardName(Preset::A3Landscape)) == "A3");
    assert(std::string(SheetPresenter::standardName(Preset::A2Landscape)) == "A2");
    assert(std::string(SheetPresenter::standardName(Preset::A1Landscape)) == "A1");
}
static void test_presetFromStandard()
{
    assert(SheetPresenter::presetFromStandard("A4") == Preset::A4Landscape);
    assert(SheetPresenter::presetFromStandard("a3") == Preset::A3Landscape);
    assert(SheetPresenter::presetFromStandard("A2") == Preset::A2Landscape);
    assert(SheetPresenter::presetFromStandard("a1") == Preset::A1Landscape);
    // Unknown/empty defaults to A4
    assert(SheetPresenter::presetFromStandard("")   == Preset::A4Landscape);
    assert(SheetPresenter::presetFromStandard("A5") == Preset::A4Landscape);
}
int main()
{
    test_sizeForPreset();
    test_standardName();
    test_presetFromStandard();
    return 0;
}
