const std = @import("std");
const zcc = @import("compile_commands");

const cxx_flags: []const []const u8 = &.{
    "-Wall",
    "-Wshadow",
    "-isystem", "3rdparty",
    "-DYYJSON_DISABLE_INCR_READER=1",
    "-DYYJSON_DISABLE_UTILS=1",
    "-DYYJSON_DISABLE_FAST_FP_CONV=1",
    "-DYYJSON_DISABLE_NON_STANDARD=1",
    // Cross-compilation builds fail for avx512, just disable for now
    // TODO maybe figure out how to enable avx512 support eventually
    "-DSIMDUTF_IMPLEMENTATION_ICELAKE=0",
    "-DSIMDUTF_NO_LIBCXX=1",
};

const c_flags: []const []const u8 = .{
    "-std=c99",
} ++ cxx_flags;

const cpp_flags: []const []const u8 = .{
    "-std=c++20",
    "-fno-exceptions",
    "-fno-rtti",
} ++ cxx_flags;

const blitter_sources: []const []const u8 = &.{
    "src/main.cpp",
    "src/base.cpp",
};

const xao_sources: []const []const u8 = &.{
    "src/xao.c",
};

const yyjson_sources: []const []const u8 = &.{
    "3rdparty/yyjson.c",
};

const simdutf_sources: []const []const u8 = &.{
    "3rdparty/simdutf.cpp",
};

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    var cdb_targets: std.ArrayList(*std.Build.Step.Compile) = .empty;

    const blitter_sources_plat = switch (target.result.os.tag) {
        .macos => blitter_sources ++ .{ "src/platform_posix.cpp" },
        .linux => blitter_sources ++ .{ "src/platform_posix.cpp" },
        .windows => blitter_sources ++ .{ "src/platform_windows.cpp" },
        else => @panic("Unsupported OS"),
    };

    const sdl = b.dependency("sdl", .{ .optimize = optimize, .target = target });
    try cdb_targets.append(b.allocator, sdl.artifact("SDL3"));

    const sdl_ttf = b.dependency("SDL_ttf", .{ .optimize = optimize, .target = target });
    try cdb_targets.append(b.allocator, sdl_ttf.artifact("SDL3_ttf"));

    // const raylib = b.dependency("raylib", .{ .optimize = optimize, .target = target });
    // try targets.append(b.allocator, raylib.artifact("raylib"));

    // Build simdutf separately so we can compile with libcpp headers, but not
    // link libcpp in final executable
    // TODO: do we need to build main.cpp with the C header to get small build
    // size? See singlefile in downloads
    const simdutf = b.addLibrary(.{
        .name = "simdutf",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libcpp = true,
        }),
    });
    simdutf.root_module.addCSourceFiles(.{ .files = simdutf_sources, .flags = cpp_flags });
    simdutf.installHeader(b.path("3rdparty/simdutf_c.h"), "simdutf_c.h");
    try cdb_targets.append(b.allocator, simdutf);

    const yyjson = b.addLibrary(.{
        .name = "yyjson",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    yyjson.root_module.addCSourceFiles(.{ .files = yyjson_sources, .flags = c_flags, });
    yyjson.installHeader(b.path("3rdparty/yyjson.h"), "yyjson.h");
    try cdb_targets.append(b.allocator, yyjson);

    const xao = b.addLibrary(.{
        .name = "xao",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    xao.root_module.addCSourceFiles(.{ .files = xao_sources, .flags = c_flags });
    xao.installHeader(b.path("src/xao.h"), "xao.h");
    try cdb_targets.append(b.allocator, xao);

    const blitter = b.addExecutable(.{
        .name = "blitter",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .strip = false,
            .link_libc = true,
            .link_libcpp = false,
        }),
    });
    blitter.root_module.addCSourceFiles(.{ .files = blitter_sources_plat, .flags = cpp_flags });
    blitter.root_module.linkLibrary(simdutf);
    blitter.root_module.linkLibrary(yyjson);
    blitter.root_module.linkLibrary(xao);
    blitter.root_module.linkLibrary(sdl.artifact("SDL3"));
    blitter.root_module.linkLibrary(sdl_ttf.artifact("SDL3_ttf"));
    // blitter.root_module.linkLibrary(raylib.artifact("raylib"));
    try cdb_targets.append(b.allocator, blitter);

    b.installArtifact(blitter);
    const run_blitter = b.addRunArtifact(blitter);
    if (b.args) |args| {
        run_blitter.addArgs(args);
    }
    const run_step = b.step("run", "Run the application");
    run_step.dependOn(&run_blitter.step);

    _ = zcc.createStep(b, "cdb", try cdb_targets.toOwnedSlice(b.allocator));
}
