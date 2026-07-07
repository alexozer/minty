const std = @import("std");
const zcc = @import("compile_commands");

const thirdparty_c_sources: []const []const u8 = &.{
    "3rdparty/xao.c",
    "3rdparty/yyjson.c",
    "3rdparty/kb_text_shape.c",
    "3rdparty/stb_rect_pack.c",
};

// Build in separate library to (maybe?) avoid linking libcpp
const simdutf_sources: []const []const u8 = &.{
    "3rdparty/simdutf.cpp",
};

const cxx_flags: []const []const u8 = &.{
    "-Wall",
    "-Wshadow",
    "-Wconversion",
    "-Wimplicit-fallthrough",
    "-isystem",
    "3rdparty",
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
    "-std=c23",
} ++ cxx_flags;

const cpp_flags: []const []const u8 = .{
    "-std=c++20",
    "-fno-exceptions",
    "-fno-rtti",
} ++ cxx_flags;

pub fn getSourceFiles(
    b: *std.Build,
    dir_path: []const u8,
    extension: []const u8,
) ![][]const u8 {
    var file_list = std.ArrayList([]const u8).empty;
    errdefer file_list.deinit(b.allocator);

    const dir = b.build_root.handle;
    const src_dir = try dir.openDir(b.graph.io, dir_path, .{
        .access_sub_paths = true,
        .iterate = true,
        .follow_symlinks = false,
    });
    defer src_dir.close(b.graph.io);

    var walker = try src_dir.walk(b.allocator);
    defer walker.deinit();

    while (try walker.next(b.graph.io)) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.endsWith(u8, entry.basename, extension)) continue;
        if (std.mem.startsWith(u8, entry.basename, "platform")) continue;
        // TODO exclude entire "tools" dir
        if (std.mem.startsWith(u8, entry.basename, "codegen")) continue;

        const path = try std.fs.path.join(b.allocator, &.{ dir_path, entry.path });
        try file_list.append(b.allocator, path);
    }

    return try file_list.toOwnedSlice(b.allocator);
}

fn get_simdutf_library(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Step.Compile {
    const simdutf = b.addLibrary(.{
        .name = "simdutf",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            // We attempt to configure simdutf to not actually depend on linking against libc++, but
            // it still needs std headers
            .link_libcpp = true,
        }),
    });
    simdutf.root_module.addCSourceFiles(.{
        .files = simdutf_sources,
        .flags = cpp_flags,
        .language = .cpp,
    });
    simdutf.installHeader(b.path("3rdparty/simdutf_c.h"), "simdutf_c.h");
    return simdutf;
}

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const native_target = b.resolveTargetQuery(.{ .cpu_model = .native });
    const native_optimize: std.builtin.OptimizeMode = .ReleaseFast;

    var cdb_targets: std.ArrayList(*std.Build.Step.Compile) = .empty;

    const blitter_sources = try getSourceFiles(b, "src", ".c");

    const platform_sources: []const []const u8 = switch (target.result.os.tag) {
        .macos => &.{ "src/platform_posix.c", "src/platform_macos.c" },
        .linux => &.{ "src/platform_posix.c", "src/platform_linux.c" },
        .windows => &.{"src/platform_windows.c"},
        else => @panic("Unsupported OS"),
    };
    const blitter_flags = c_flags ++ .{"-Werror"};

    const sdl = b.dependency("sdl", .{ .optimize = optimize, .target = target });
    const sdl_native = b.dependency("sdl", .{ .target = native_target, .optimize = native_optimize });
    try cdb_targets.append(b.allocator, sdl.artifact("SDL3"));

    const freetype = b.dependency("freetype", .{ .optimize = optimize, .target = target });
    try cdb_targets.append(b.allocator, freetype.artifact("freetype"));

    const simdutf = get_simdutf_library(b, target, optimize);
    const simdutf_native = get_simdutf_library(b, native_target, native_optimize);
    try cdb_targets.append(b.allocator, simdutf);

    //
    // codegen
    //

    const codegen_sources = &.{ "src/tools/codegen.c", "src/base.c" };
    var codegen_sources_plat: std.ArrayList([]const u8) = .empty;
    try codegen_sources_plat.appendSlice(b.allocator, codegen_sources);
    try codegen_sources_plat.appendSlice(b.allocator, platform_sources);

    const codegen = b.addExecutable(.{
        .name = "codegen",
        .root_module = b.createModule(.{
            .target = native_target,
            .optimize = native_optimize,
            .strip = false,
            .link_libc = false,
            .link_libcpp = false,
        }),
    });
    codegen.root_module.addCSourceFiles(.{
        .files = try codegen_sources_plat.toOwnedSlice(b.allocator),
        .flags = blitter_flags,
        .language = .c,
    });
    codegen.root_module.linkLibrary(sdl_native.artifact("SDL3"));
    codegen.root_module.linkLibrary(simdutf_native);
    try cdb_targets.append(b.allocator, codegen);
    b.installArtifact(codegen);

    //
    // blitter
    //

    var blitter_sources_plat: std.ArrayList([]const u8) = .empty;
    try blitter_sources_plat.appendSlice(b.allocator, blitter_sources);
    try blitter_sources_plat.appendSlice(b.allocator, platform_sources);

    const blitter = b.addExecutable(.{
        .name = "blitter",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .strip = false,
            .link_libc = false, // Does SDL pull it in anyway?
            .link_libcpp = false,
        }),
    });
    blitter.root_module.addCSourceFiles(.{
        .files = try blitter_sources_plat.toOwnedSlice(b.allocator),
        .flags = blitter_flags,
        .language = .c,
    });
    blitter.root_module.addCSourceFiles(.{
        .files = thirdparty_c_sources,
        .flags = c_flags,
        .language = .c,
    });
    blitter.root_module.linkLibrary(simdutf);
    blitter.root_module.linkLibrary(sdl.artifact("SDL3"));
    blitter.root_module.linkLibrary(freetype.artifact("freetype"));
    try cdb_targets.append(b.allocator, blitter);
    b.installArtifact(blitter);

    // Make Blitter depend on codegen
    const codegen_step = b.addRunArtifact(codegen);
    for (blitter_sources) |source| {
        if (std.mem.endsWith(u8, source, "base.c")) continue;
        codegen_step.addFileArg(b.path(source));
    }
    blitter.step.dependOn(&codegen_step.step);

    const run_blitter = b.addRunArtifact(blitter);
    if (b.args) |args| {
        run_blitter.addArgs(args);
    }
    const run_step = b.step("run", "Run the application");
    run_step.dependOn(&run_blitter.step);

    _ = zcc.createStep(b, "cdb", try cdb_targets.toOwnedSlice(b.allocator));
}
