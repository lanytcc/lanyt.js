
add_rules("mode.debug", "mode.release")
add_requires("mimalloc")
add_packages("mimalloc")
if is_plat("windows") then
    add_requires("pthreads4w")
    add_packages("pthreads4w")
    add_syslinks("ws2_32", "wldap32")
end

option("bignum")
    set_default(false)
    set_showmenu(true)
    set_description("Enable quickjs bignum support")
    add_defines("CONFIG_BIGNUM")
option_end()

option("atomics")
    set_default(true)
    set_showmenu(true)
    set_description("Enable quickjs atomics support")
    add_defines("CONFIG_ATOMICS")
option_end()

option("platform")
    set_default(true)
    set_showmenu(true)
    set_description("Enable quickjs platform support")
    if is_plat("windows") then
        add_cxflags("-DJS_STRICT_NAN_BOXING")
        if is_arch("x64") then
            add_defines("ssize_t=__int64")
        elseif is_arch("x86") then
            add_defines("ssize_t=int")
        end
    end
option_end()

option("smallest")
    set_default(false)
    set_showmenu(true)
    set_description("Enable exe smallest size")
    set_optimize("smallest")
option_end()

target("quickjs")
    set_kind("static")
    set_languages("c11")
    add_includedirs("quickjs", {public = true})
    add_files("jsc.c", "module.c", "quickjs/lib*.c", "quickjs/quickjs*.c", "quickjs/cutils.c")
    add_options("bignum", "atomics", "platform", "jsx", "smallest")
target_end()

target("pjs")
    add_deps("quickjs")
    set_kind("binary")
    set_languages("c11")
    add_files("main.c")
    add_options("bignum", "atomics", "platform", "jsx", "smallest")
target_end()
