pdshpy
======

Pdshpy (pronounced however you like; we sometimes say "pudge-pie") acts as a
[pdsh ("Parallel distributed shell")](https://code.google.com/p/pdsh/) module
which loads a Python module and proxies certain functionality to it, so that
host working set collection, host exclusion, and any other pdsh option
rearrangement can be done in plain Python.

When `pdshpy.so` is in pdsh's modules dir (`/usr/lib/pdsh` here), then when
pdsh loads its modules, pdshpy looks for a Python module in the `PYTHONPATH`
called "`pdshpy_module`". If it successfully imports, pdshpy will call the
module's `initialize()` function and register any command-line options that the
module supports with pdsh, so that they will show up in `-h` help output and be
called back when the option is used.

The Python module may also include functionality to change the pdsh options or
add or remove things from the pdsh host working set. See
`pdshpy_module_sample.py` for more explanation and detail on the supported
interface.

This source includes a snapshot of pdsh's header files, since a module needs to
be compiled against the same (or a compatible) set of headers in order to work
on the same objects in memory and link properly at runtime. If you need pdshpy
to work against a different version of pdsh, just replace the headers.
