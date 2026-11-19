# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Parameter values of an element, checked as builtin_element_registry.cpp does."""
import math

from .errors import SchemaError


def _number(params: dict, name: str, default: float) -> float:
    value = params.get(name, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SchemaError(f"parameter '{name}' must be a number (s7py supports F64 signals only)")
    return float(value)


def _numbers(params: dict, name: str, default=(1, 1)) -> list[float]:
    values = params.get(name, list(default))
    if not isinstance(values, list) or not values:
        raise SchemaError(f"parameter '{name}' must be a non-empty array of numbers")
    if len(values) == 1 or len(values) > 64:
        raise SchemaError(f"parameter '{name}' must have 2 to 64 entries")
    return [_number({name: value}, name, 0.0) for value in values]


def _integer(params: dict, name: str, default: int, low: int, high: int) -> int:
    """An "int" parameter: a whole number, possibly written as 3.0, within [low, high]."""
    value = params.get(name, default)
    whole = isinstance(value, int) or (isinstance(value, float) and math.isfinite(value) and value.is_integer())
    if isinstance(value, bool) or not whole:
        raise SchemaError(f"parameter '{name}' must be an integer")
    if value < low:
        raise SchemaError(f"parameter '{name}' must be at least {low}")
    if value > high:
        raise SchemaError(f"parameter '{name}' must be at most {high}")
    return int(value)


def _flag(params: dict, name: str, default: bool) -> bool:
    value = params.get(name, default)
    if not isinstance(value, bool):
        raise SchemaError(f"parameter '{name}' must be true or false")
    return value


def _choice(params: dict, name: str, allowed: tuple[str, ...]) -> str:
    value = params.get(name, allowed[0])
    if value not in allowed:
        raise SchemaError(f"parameter '{name}' must be one of {', '.join(allowed)}")
    return value


def _check_params(params: dict, known: tuple[str, ...]) -> None:
    """Every parameter is declared (known: the declared names), and a type pin is F64."""
    for name in params:
        if name not in known:
            raise SchemaError(f"unknown parameter '{name}' " + (f"(known: {', '.join(known)})" if known else "(it has none)"))
    for name in ("type", "accumulator_type"):
        if params.get(name, "F64") != "F64":
            raise SchemaError(f"parameter '{name}': type {params[name]} is not supported (F64 only)")
    if "overflow" in params:
        _choice(params, "overflow", ("saturate", "wrap"))
