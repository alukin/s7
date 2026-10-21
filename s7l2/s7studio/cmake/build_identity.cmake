# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

execute_process(COMMAND git -C "${SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE FOUND)
if(NOT FOUND EQUAL 0)
    set(REVISION "unknown")
endif()
execute_process(COMMAND git -C "${SOURCE_DIR}" diff --quiet HEAD -- . RESULT_VARIABLE DIRTY ERROR_QUIET)
if(DIRTY EQUAL 0)
    set(DIRTY 0)
else()
    set(DIRTY 1)
endif()
file(CONFIGURE OUTPUT "${OUTPUT}" CONTENT
"#pragma once
#define S7_BUILD_VERSION \"@VERSION@\"
#define S7_BUILD_REVISION \"@REVISION@\"
#define S7_BUILD_DIRTY @DIRTY@
#define S7_BUILD_COMPILER \"@COMPILER@\"
" @ONLY)
