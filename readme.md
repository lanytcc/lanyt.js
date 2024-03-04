# A tiny js interpreter

## Introduction

A JavaScript interpreter based on a modified version of QuickJS that supports Windows. It can convert JavaScript code into bytecode and execute it at any time from JavaScript files or bytecode.

## Features:
1. I used a version of the QuickJS library that supports Windows, and it now supports dynamic library imports on three PC platforms.
2. Supports direct compilation of JS into small-sized binary bytecode, as well as execution from this bytecode.
3. Supports executing code from strings; use strings with a `<lanyt>` prefix when calling `lanyt_js_eval` for execution.
4. Simple and easy-to-use API.

## How to use:
1. Download the library and the necessary QuickJS dependencies.

```shell
git clone https://github.com/lanytcc/lanyt.js.git
git clone https://github.com/lanytcc/quickjs.git
```

2. Compile and install the binary.

```shell
cd lanyt.js
# xmake f -cc=clang #if you use windows, you need use this command
xmake b ljs 
xmake install -o /usr/local ljs # install to /usr/local
```

3. Use the binary in your command line.

```shell
ljs run test.js # more details see `ljs --help`
```