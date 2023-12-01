
add_rules("mode.debug", "mode.release")
add_requires("mimalloc", "pthreads4w")

option("qjs-atomics")
    set_default(false)
    set_showmenu(true)
    set_description("Enable quickjs atomics support")
    add_defines("CONFIG_ATOMICS")
option_end()

option("qjs-platform")
    set_default(true)
    set_showmenu(true)
    set_description("Enable quickjs platform support")
    if is_plat("windows") then
        add_cxflags("-DJS_STRICT_NAN_BOXING")
        -- 64位
        if is_arch("x64") then
            add_defines("ssize_t=__int64")
        elseif is_arch("x86") then
            add_defines("ssize_t=int")
        end
    end
option_end()

target("quickjs")
    set_kind("static")
    set_languages("c11")
    add_files("quickjs/lib*.c", "quickjs/quickjs*.c", "quickjs/cutils.c")
    add_options("qjs-atomics", "qjs-platform")
    add_packages("mimalloc")
    if is_plat("windows") then
        add_packages("pthreads4w")
    end
    before_build(function (target) 
        if not os.isfile("quickjs/quickjs-version.h") then
            local ver = io.open("quickjs/VERSION", "r"):read("*a")
            local file = io.open("quickjs/quickjs-version.h", "w+")
            file:write("#define QUICKJS_VERSION \"" .. ver .. "\"\n")
            file:close()
        end
    end)
target_end()

target("pjs")
    add_deps("quickjs")
    set_kind("binary")
    set_languages("c11")
    add_includedirs("quickjs", ".")
    add_files("*.c")
    add_packages("mimalloc")
    add_options("qjs-atomics", "qjs-platform")
    if is_plat("windows") then
        add_packages("pthreads4w")
    end