/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <xdp-certificate/ui.h>

// Complete headless UI for SDK tests that never collect credentials.
inline XccUiV1 certificateTestUi()
{
    XccUiV1 ui{ };
    ui.struct_size = sizeof(ui);
    ui.abi_version = XCC_ABI_VERSION;
    ui.chooser_show = [](void *, XccUiId, const XccChooserView *) { };
    ui.chooser_update = ui.chooser_show;
    ui.pin_show = [](void *, XccUiId, const XccPinView *) { };
    ui.pin_update = ui.pin_show;
    ui.warning = [](void *, XccUiId, const XccWarning *) { };
    ui.hide = [](void *, XccUiId) { };
    ui.close = ui.hide;
    return ui;
}
