"""Python package that exposes the compiled Rindle extension module."""

from importlib import metadata as _metadata

from . import rindle as _bindings

__doc__ = _bindings.__doc__
__all__ = getattr(_bindings, "__all__", [name for name in dir(_bindings) if not name.startswith("_")])

for name in __all__:
    globals()[name] = getattr(_bindings, name)


def __getattr__(name: str):
    return getattr(_bindings, name)


def __dir__():
    return sorted(set(__all__) | set(globals()) | set(dir(_bindings)))


try:
    __version__ = _metadata.version("rindle")
except _metadata.PackageNotFoundError:  # pragma: no cover - local editable installs
    __version__ = "0.0.dev0"
