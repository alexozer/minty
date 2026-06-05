const std = @import("std");
const zcc = @import("compile_commands");

const cxx_flags: []const []const u8 = &.{
    "-Wall",
    "-Wshadow",
    "-isystem", "3rdparty",
    "-DSIMDUTF_NO_LIBCXX=1",
    "-DYYJSON_DISABLE_INCR_READER",
    "-DYYJSON_DISABLE_UTILS",
    "-DYYJSON_DISABLE_FAST_FP_CONV",
    "-DYYJSON_DISABLE_NON_STANDARD",
};

const c_flags: []const []const u8 = .{
    "-std=c99",
} ++ cxx_flags;

const cpp_flags: []const []const u8 = .{
    "-std=c++20",
    "-fno-exceptions",
    "-fno-rtti",
} ++ cxx_flags;

const c_sources: []const []const u8 = &.{
    "src/xao.c",
    "3rdparty/yyjson.c",
};

const cpp_sources: []const []const u8 = &.{
    "src/main.cpp",
    "src/base.cpp",
    "src/platform_macos.cpp",
    "src/platform_posix.cpp",
};

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    var targets: std.ArrayList(*std.Build.Step.Compile) = .empty;

    // const sdl = b.dependency("sdl", .{ .optimize = optimize, .target = target });
    // try targets.append(b.allocator, sdl.artifact("SDL3"));

    const raylib = b.dependency("raylib", .{ .optimize = optimize, .target = target });
    try targets.append(b.allocator, raylib.artifact("raylib"));

    // Build simdutf separately so we can compile with libcpp headers, but not
    // link libcpp in final executable. TODO are we actually avoiding libcpp?
    const simdutf = b.addLibrary(.{
        .name = "simdutf",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libcpp = true,
        }),
    });
    simdutf.root_module.addCSourceFiles(.{
        .files = &.{ "3rdparty/simdutf.cpp" },
        .flags = cpp_flags,
        .language = .cpp,
    });
    simdutf.installHeader(b.path("3rdparty/simdutf_c.h"), "simdutf_c.h");
    try targets.append(b.allocator, simdutf);

    const blitter = b.addExecutable(.{
        .name = "blitter",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .strip = optimize != .Debug,
            .link_libc = true,
            .link_libcpp = false,
        }),
    });
    blitter.root_module.addCSourceFiles(.{
        .files = cpp_sources,
        .flags = cpp_flags,
        .language = .cpp,
    });
    blitter.root_module.addCSourceFiles(.{
        .files = c_sources,
        .flags = c_flags,
        .language = .c,
    });
    blitter.root_module.linkLibrary(simdutf);
    // blitter.root_module.linkLibrary(sdl.artifact("SDL3"));
    blitter.root_module.linkLibrary(raylib.artifact("raylib"));
    try targets.append(b.allocator, blitter);

    b.installArtifact(blitter);
    const run_blitter = b.addRunArtifact(blitter);
    if (b.args) |args| {
        run_blitter.addArgs(args);
    }
    const run_step = b.step("run", "Run the application");
    run_step.dependOn(&run_blitter.step);

    _ = zcc.createStep(b, "cdb", try targets.toOwnedSlice(b.allocator));
}
