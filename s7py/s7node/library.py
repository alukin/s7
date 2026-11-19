# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Element libraries a node loads at start (--lib): Python modules defining element types.

A library is a .py file or an importable module name. If it defines register(registry), the node
calls it; otherwise every public s7core.Element subclass the module itself defines is registered
under its type_name. A library runs with the node's privileges: load only code you trust.
"""
import importlib
import importlib.util
import inspect
import sys
from pathlib import Path

from s7core.element import Element
from s7core.registry import Registry


class LibraryError(Exception):
    """A library that cannot be loaded or registers a bad element type."""


def import_library(spec: str):
    """The module named by spec: a path to a .py file, or a module name."""
    path = Path(spec)
    if path.suffix == ".py" or path.exists():
        if not path.is_file():
            raise LibraryError(f"library '{spec}': no such file")
        name = f"s7lib_user_{path.stem}"
        module_spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(module_spec)
        sys.modules[name] = module
        module_spec.loader.exec_module(module)
        return module
    return importlib.import_module(spec)


def load_libraries(specs, base: Registry) -> Registry:
    """A new registry holding base's types and those of every library in specs, in order."""
    registry = Registry(base)
    for spec in specs:
        try:
            module = import_library(spec)
            if callable(getattr(module, "register", None)):
                module.register(registry)
                continue
            for name, value in vars(module).items():
                if (inspect.isclass(value) and issubclass(value, Element) and value.__module__ == module.__name__
                        and not name.startswith("_")):
                    registry.register(value)
        except LibraryError:
            raise
        except Exception as error:  # an import error, or a DeclarationError from register()
            raise LibraryError(f"library '{spec}': {type(error).__name__}: {error}") from error
    return registry
