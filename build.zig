const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});

    const exe = b.addExecutable(.{
        .name = "ljs",
        .target = target,
        .optimize = .ReleaseSafe,
    });

    exe.linkLibC();
    exe.addIncludePath(.{ .path = "../quickjs" });
    exe.addLibraryPath(.{ .path = "../quickjs/zig-out/lib" });
    exe.linkSystemLibrary("quickjs");
    exe.linkSystemLibrary("c");
    exe.addCSourceFiles(.{
        .files = &.{ "main.c", "jsc.c", "module.c" },
        .flags = &.{
            "-Wall",
            "-Wno-array-bounds",
            "-fwrapv",
            "-fdeclspec",
            "-fvisibility=hidden",
            "-DCONFIG_VERSION=\"2024-02-14\"",
            // "-DCONFIG_CHECK_JSVALUE",
        },
    });

    b.installArtifact(exe);
}
