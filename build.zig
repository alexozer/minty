const std = @import("std");

const blitter_sources: []const []const u8 = &.{
    "src/main.cpp",
    "src/base.cpp",
    "src/platform_macos.cpp",
    "src/platform_posix.cpp",
};

const blitter_flags: []const []const u8 = &.{
    "-std=c++20",
    "-fno-exceptions",
    "-fno-rtti",
    "-Wall",
    "-Wshadow",
};

const xao_sources: []const []const u8 = &.{
    "src/xao.c",
};

const xao_flags: []const []const u8 = &.{
    "-std=c99",
    "-Wall",
    "-Wshadow",
};

const simdutf_sources: []const []const u8 = &.{
    "3rdparty/simdutf.cpp",
};

const simdutf_flags: []const []const u8 = &.{
    "-std=c++20",
    "-fno-exceptions",
    "-fno-rtti",
    "-DSIMDUTF_NO_LIBCXX=1",
};

const yyjson_sources: []const []const u8 = &.{
    "3rdparty/yyjson.c",
};

const yyjson_flags: []const []const u8 = &.{
    "-std=c99",
    "-Wall",
    "-Wshadow",
    "-DYYJSON_DISABLE_INCR_READER",
    "-DYYJSON_DISABLE_UTILS",
    "-DYYJSON_DISABLE_FAST_FP_CONV",
    "-DYYJSON_DISABLE_NON_STANDARD",
};


pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Build simdutf separately so we can compile with libcpp headers, but not
    // link libcpp in final executable
    const simdutf = b.addLibrary(.{
        .name = "simdutf",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libcpp = true,
        }),
    });
    simdutf.root_module.addCSourceFiles(.{
        .files = simdutf_sources,
        .flags = simdutf_flags,
    });
    simdutf.installHeader(b.path("3rdparty/simdutf.h"), "simdutf.h");

    const yyjson = b.addLibrary(.{
        .name = "yyjson",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });
    yyjson.root_module.addCSourceFiles(.{
        .files = yyjson_sources,
        .flags = yyjson_flags,
    });
    yyjson.installHeader(b.path("3rdparty/yyjson.h"), "yyjson.h");

    const xao = b.addLibrary(.{
        .name = "xao",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });
    xao.root_module.addCSourceFiles(.{
        .files = xao_sources,
        .flags = xao_flags,
    });
    xao.installHeader(b.path("src/xao.h"), "xao.h");

    const blitter = b.addExecutable(.{
        .name = "blitter",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .strip = true,
            .link_libc = true,
            .link_libcpp = false,
        }),
    });
    blitter.root_module.addCSourceFiles(.{
        .files = blitter_sources,
        .flags = blitter_flags,
    });
    blitter.root_module.linkLibrary(simdutf);
    blitter.root_module.linkLibrary(yyjson);
    blitter.root_module.linkLibrary(xao);

    b.installArtifact(blitter);
}
