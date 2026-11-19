# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Port declarations shared by the built-in elements, which are generic over T as in s7core."""
from s7core.spec import Port


def inp(name: str = "in", description: str = "Input", rule: str = "T") -> Port:
    return Port(name, description, type_rule=rule)


def out(name: str = "out", description: str = "Output", rule: str = "T") -> Port:
    return Port(name, description, type_rule=rule)


IN, OUT = (inp(),), (out(),)
