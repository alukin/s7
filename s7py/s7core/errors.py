# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Errors of the S7 core."""


class SchemaError(ValueError):
    pass


class DeclarationError(Exception):
    """An element type whose declaration is inconsistent; raised when it is registered."""
