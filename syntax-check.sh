#!/usr/bin/env bash
# AluChop — single translation-unit syntax check.
# Usage: ./syntax-check.sh src/models/Order.cpp [more files...]
#
# Homebrew Qt 6.11.1 is a FRAMEWORK build: real headers live in
# lib/<Module>.framework/Headers, NOT in include/. See TOOLCHAIN.md.
set -euo pipefail

cd "$(dirname "$0")"

QB=/opt/homebrew/opt/qtbase/lib
QC=/opt/homebrew/opt/qtcharts/lib
QS=/opt/homebrew/opt/qtsvg/lib

if [ $# -eq 0 ]; then
  echo "usage: $0 <file.cpp|file.hpp> [...]" >&2
  exit 2
fi

exec clang++ -fsyntax-only -std=c++17 -Wall -Wextra \
  -I include \
  -F "$QB" -F "$QC" -F "$QS" \
  -I "$QB/QtCore.framework/Headers" \
  -I "$QB/QtGui.framework/Headers" \
  -I "$QB/QtWidgets.framework/Headers" \
  -I "$QB/QtSql.framework/Headers" \
  -I "$QB/QtPrintSupport.framework/Headers" \
  -I "$QC/QtCharts.framework/Headers" \
  -I "$QS/QtSvg.framework/Headers" \
  -I "$QS/QtSvgWidgets.framework/Headers" \
  "$@"
