#include "RtlBiasTWidget.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>

TEST_CASE("RTL bias-T UI requires capability and explicit power confirmation", "[rtl-bias][gui]") {
    RtlBiasTWidget widget; widget.resize(520, 130); widget.show();
    auto* box = widget.findChild<QCheckBox*>("rtlBiasT");
    auto* status = widget.findChild<QLabel*>("rtlBiasTStatus");
    REQUIRE(box); REQUIRE(status); CHECK_FALSE(box->isEnabled()); CHECK_FALSE(box->isChecked());
    RtlBiasT::State state; state.probed = true;
    widget.setState(state); CHECK_FALSE(box->isEnabled());
    state.supported = true; state.status = "Driver reports OFF (voltage not measured)";
    widget.setState(state); CHECK(box->isEnabled());
    int writes = 0, confirms = 0; bool confirm = false, fail = false;
    widget.confirmPower = [&] { ++confirms; return confirm; };
    widget.apply = [&](bool on, std::string& error) {
        ++writes;
        if (fail) { error = "Driver write rejected"; return false; }
        state.enabled = on; return true;
    };
    box->click(); CHECK(confirms == 1); CHECK(writes == 0); CHECK_FALSE(box->isChecked());
    confirm = true; box->click(); CHECK(box->isChecked()); CHECK(state.enabled); CHECK(writes == 1);
    box->click(); CHECK_FALSE(box->isChecked()); CHECK_FALSE(state.enabled); CHECK(confirms == 2);
    fail = true; box->click(); CHECK_FALSE(box->isChecked()); CHECK_FALSE(state.enabled);
    CHECK(status->text().contains("rejected"));
    const int before = writes;
    for (int n = 0; n < 100; ++n) widget.setState(state);
    CHECK(writes == before);
    QApplication::processEvents();
    if (qEnvironmentVariableIsSet("SDR_TOWN_RTL_GUI_SCREENSHOT"))
        CHECK(widget.grab().save(qEnvironmentVariable("SDR_TOWN_RTL_GUI_SCREENSHOT")));
}
