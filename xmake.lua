
add_rules("mode.debug", "mode.release")
add_requires("mimalloc")
includes("../quickjs/xmake.lua")

target("ljs")
    add_packages("mimalloc")
    add_deps("quickjs")
    set_kind("binary")
    set_languages("c11")
    add_files("main.c","jsc.c", "module.c")
    add_options("bignum", "atomics", "platform", "jsx", "smallest")
target_end()