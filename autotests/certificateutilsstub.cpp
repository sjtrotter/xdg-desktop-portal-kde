/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

// The adaptor test runs offscreen; parent import needs the Wayland integration, which it does not link.
#include "utils.h"

void Utils::setParentWindow(QWidget *, const QString &)
{
}

void Utils::setParentWindow(QWindow *, const QString &)
{
}
