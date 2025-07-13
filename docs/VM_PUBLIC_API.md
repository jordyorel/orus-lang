# Orus VM Public API

This document describes the key functions available to embedders of the Orus virtual machine.

- **initVM()** – Initialize the global VM state. Must be called before executing any code.
- **freeVM()** – Release resources allocated by the VM. Call once execution is finished.
- **warmupVM()** – Optional startup routine that prepares dispatch tables and caches.
- **interpret()** – Execute a string of Orus source code.
- **interpret_module()** – Execute a module from a file path.

Utility helpers such as `allocateString()` and `collectGarbage()` are also exposed via `vm.h` for advanced integrations.
