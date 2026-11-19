# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The element types a node serves: the built-in library and the network stream ends."""
from s7core.registry import Registry
from s7lib import LIBRARY

from .net import NetSink, NetSource

NODE_ELEMENT_TYPES = Registry(LIBRARY)
NODE_ELEMENT_TYPES.register(NetSink)
NODE_ELEMENT_TYPES.register(NetSource)
