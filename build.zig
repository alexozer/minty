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
const blitter_flags = c_flags ++ .{"-Werror"};

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

fn get_simdutf_library(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) *std.Build.Step.Compile {
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

fn get_codegen_step(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    sdl: *std.Build.Dependency,
    simdutf: *std.Build.Step.Compile,
) !*std.Build.Step.Compile {
    const codegen_sources = &.{ "src/tools/codegen.c", "src/base.c" };
    const platform_sources = get_platform_sources(target);

    var codegen_sources_plat: std.ArrayList([]const u8) = .empty;
    try codegen_sources_plat.appendSlice(b.allocator, codegen_sources);
    try codegen_sources_plat.appendSlice(b.allocator, platform_sources);

    const codegen = b.addExecutable(.{
        .name = "codegen",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .strip = false,
            .link_libc = true,
            .link_libcpp = false,
        }),
    });
    codegen.root_module.addCSourceFiles(.{
        .files = try codegen_sources_plat.toOwnedSlice(b.allocator),
        .flags = blitter_flags,
        .language = .c,
    });
    codegen.root_module.linkLibrary(sdl.artifact("SDL3"));
    codegen.root_module.linkLibrary(simdutf);
    return codegen;
}

fn get_xxd_step(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) !*std.Build.Step.Compile {
    const xxd_sources = &.{"3rdparty/xxd.c"};

    const xxd = b.addExecutable(.{
        .name = "codegen",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .strip = false,
            .link_libc = true,
            .link_libcpp = false,
        }),
    });
    xxd.root_module.addCSourceFiles(.{
        .files = xxd_sources,
        .flags = blitter_flags,
        .language = .c,
    });
    return xxd;
}

const ShaderStage = enum {
    vertex,
    fragment,
};

const ShaderTarget = enum {
    dxil,
    msl,
    spirv,
};

fn add_shadercross_dep(
    b: *std.Build,
    blitter: *std.Build.Step.Compile,
    xxd: *std.Build.Step.Compile,
    shader_source_path: []const u8,
    stage: ShaderStage,
    target: ShaderTarget,
) !void {
    const dest_target = try std.ascii.allocUpperString(b.allocator, @tagName(target));
    const out_filename = try std.fmt.allocPrint(
        b.allocator,
        "{s}.{s}",
        .{ std.fs.path.stem(shader_source_path), @tagName(target) },
    );
    const out_path = try std.fs.path.join(b.allocator, &.{
        "shaders",
        out_filename,
    });

    const shadercross = b.addSystemCommand(&.{"shadercross"});
    shadercross.addFileArg(b.path(shader_source_path));
    shadercross.addArgs(&.{
        "--source",
        "hlsl",
        "--stage",
        @tagName(stage),
        "--dest",
        dest_target,
        "--output",
    });
    const compiled_shader_path = shadercross.addOutputFileArg(out_path);

    // Embed by converting compiled shader to C and including in build
    const embed_var_name = try std.fmt.allocPrint(
        b.allocator,
        "os_shader_{s}",
        .{std.fs.path.stem(shader_source_path)},
    );
    add_xxd_dep(b, blitter, xxd, compiled_shader_path, embed_var_name);

    // Output compiled shader for inspection
    const shader_install = b.addInstallFile(compiled_shader_path, out_path);
    blitter.step.dependOn(&shader_install.step);
}

// TODO avoid system xxd dependency
fn add_xxd_dep(
    b: *std.Build,
    blitter: *std.Build.Step.Compile,
    xxd: *std.Build.Step.Compile,
    source: std.Build.LazyPath,
    var_name: []const u8,
) void {
    const run_xxd = b.addRunArtifact(xxd);
    run_xxd.addArgs(&.{ "-n", var_name, "-i" });
    run_xxd.addFileArg(source);
    const output = run_xxd.captureStdOut(.{});

    blitter.root_module.addCSourceFile(.{
        .file = output,
        .language = .c,
        .flags = blitter_flags,
    });
}

fn get_platform_sources(target: std.Build.ResolvedTarget) []const []const u8 {
    return switch (target.result.os.tag) {
        .macos => &.{ "src/platform_posix.c", "src/platform_macos.c" },
        .linux => &.{ "src/platform_posix.c", "src/platform_linux.c" },
        .windows => &.{"src/platform_windows.c"},
        else => @panic("Unsupported OS"),
    };
}

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const native_target = b.resolveTargetQuery(.{ .cpu_model = .native });
    const native_optimize: std.builtin.OptimizeMode = .ReleaseFast;

    var cdb_targets: std.ArrayList(*std.Build.Step.Compile) = .empty;

    const blitter_sources = try getSourceFiles(b, "src", ".c");

    const sdl = b.dependency("sdl", .{ .optimize = optimize, .target = target });
    const sdl_native = b.dependency("sdl", .{ .target = native_target, .optimize = native_optimize });
    // try cdb_targets.append(b.allocator, sdl.artifact("SDL3"));

    const freetype = b.dependency("freetype", .{ .optimize = optimize, .target = target });
    // try cdb_targets.append(b.allocator, freetype.artifact("freetype"));

    const simdutf = get_simdutf_library(b, target, optimize);
    const simdutf_native = get_simdutf_library(b, native_target, native_optimize);
    // try cdb_targets.append(b.allocator, simdutf);

    //
    // codegen
    //

    const codegen = try get_codegen_step(b, native_target, native_optimize, sdl_native, simdutf_native);
    try cdb_targets.append(b.allocator, codegen);
    b.installArtifact(codegen);

    //
    // blitter
    //

    var blitter_sources_plat: std.ArrayList([]const u8) = .empty;
    try blitter_sources_plat.appendSlice(b.allocator, blitter_sources);
    try blitter_sources_plat.appendSlice(b.allocator, get_platform_sources(target));

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
        codegen_step.addFileInput(b.path(source));
        // Zig build system issue IMO: you really shouldn't need to call addWatchInput() on
        // dependency graph inputs
        try codegen_step.step.addWatchInput(b.path(source));
    }
    blitter.step.dependOn(&codegen_step.step);

    const xxd = try get_xxd_step(b, native_target, native_optimize);
    const shader_target: ShaderTarget = switch (target.result.os.tag) {
        .macos => .msl,
        .linux => .spirv,
        .windows => .dxil,
        else => @panic("Unsupported OS"),
    };
    try add_shadercross_dep(b, blitter, xxd, "src/shaders/vert.hlsl", .vertex, shader_target);
    try add_shadercross_dep(b, blitter, xxd, "src/shaders/frag_icon.hlsl", .fragment, shader_target);
    try add_shadercross_dep(b, blitter, xxd, "src/shaders/frag_glyph.hlsl", .fragment, shader_target);

    const run_blitter = b.addRunArtifact(blitter);
    if (b.args) |args| {
        run_blitter.addArgs(args);
    }
    const run_step = b.step("run", "Run the application");
    run_step.dependOn(&run_blitter.step);

    const cdb_step = zcc.createStep(b, "cdb", try cdb_targets.toOwnedSlice(b.allocator));
    // Ideally this should depend on every target it's generating the cdb for I suppose
    cdb_step.dependOn(&blitter.step);
}
