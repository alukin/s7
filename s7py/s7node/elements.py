# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The element types a node serves: the built-in library and the network stream ends."""
from s7core.element import Element
from s7lib import ELEMENT_TYPES as LIBRARY_TYPES

from .net import NetSink, NetSource

NODE_ELEMENT_TYPES: dict[str, type[Element]] = {**LIBRARY_TYPES, "NetSink": NetSink, "NetSource": NetSource}
