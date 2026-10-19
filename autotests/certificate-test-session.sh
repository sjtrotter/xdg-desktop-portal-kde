#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Isolate toolkit startup as well as the test's own D-Bus calls.
set -euo pipefail

unset QT_QPA_PLATFORMTHEME QT_QPA_GENERIC_PLUGINS
export QT_NO_XDG_DESKTOP_PORTAL=1
export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software
export QT_NO_GLIB=1

exec dbus-run-session --config-file="$(dirname -- "$0")/certificate-test-bus.conf" -- "$@"
