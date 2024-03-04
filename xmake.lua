
add_rules("mode.debug", "mode.release")

includes("../quickjs/xmake.lua")

target("pjs")
    add_deps("quickjs")
    set_kind("binary")
    set_languages("c11")
    add_files("main.c","jsc.c", "module.c")