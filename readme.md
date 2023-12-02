# A tiny js interpreter and debugger

## Introduction

A JavaScript interpreter based on a modified QuickJS library,  accompanied by a debugger program. It executes JS scripts as converted  bytecode. This system supports saving bytecode as binary files and  executing from these binaries. This project originated from my university graduation project, the Panda Game Engine. I use it as the  scripting system for Panda. You can compile only the QuickJS part, which includes a set of APIs I have wrapped for ease of use.

## Features:
1. I used a version of the QuickJS library that supports Windows, and it now supports dynamic library imports on three PC platforms.
2. Supports direct compilation of JS into small-sized binary bytecode, as well as execution from this bytecode.
3. Supports quick plugin imports; a single JS file lets you complete prerequisite steps to import C modules or JS modules.
4. Supports executing code from strings; use strings with a `<panda>` prefix when calling `panda_js_eval` for execution.
5. Simple and easy-to-use API.
6. A debugger that supports remote debugging (TODO).