const std = @import("std");
const zcc = @import("compile_commands");

const thirdparty_c_sources: []const []const u8 = &.{
    "3rdparty/xao.c",
    "3rdparty/yyjson.c",
    "3rdparty/kb_text_shape.c",
    "3rdparty/stb_rect_pack.c",
    "3rdparty/stb_image.c",
    "3rdparty/xxhash.c",
    "3rdparty/stb_sprintf.c",
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

const c_flags_lenient: []const []const u8 = .{
    "-std=c23",
} ++ cxx_flags;
const c_flags_strict = c_flags_lenient ++ .{"-Werror"};

const cpp_flags: []const []const u8 = .{
    "-std=c++20",
    "-fno-exceptions",
    "-fno-rtti",
} ++ cxx_flags;

pub fn getSourceFiles(
    b: *std.Build,
    dir_path: []const u8,
    extension: []const u8,
    exclude: []const u8,
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
        if (std.mem.startsWith(u8, entry.basename, exclude)) continue;

        const path = try std.fs.path.join(b.allocator, &.{ dir_path, entry.path });
        try file_list.append(b.allocator, path);
    }

    return try file_list.toOwnedSlice(b.allocator);
}

fn getSimdutfLibrary(
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

fn getCodegenStep(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
) !*std.Build.Step.Compile {
    const sdl = b.dependency("sdl", .{ .target = target, .optimize = .ReleaseFast });
    const simdutf = getSimdutfLibrary(b, target, .ReleaseFast);

    const codegen_sources = &.{ "src/tools/codegen.c", "src/base.c" };
    const platform_sources = getPlatformSources(target);

    var codegen_sources_plat: std.ArrayList([]const u8) = .empty;
    try codegen_sources_plat.appendSlice(b.allocator, codegen_sources);
    try codegen_sources_plat.appendSlice(b.allocator, platform_sources);

    const codegen = b.addExecutable(.{
        .name = "codegen",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = .ReleaseFast,
            .strip = false,
            .link_libc = true,
            .link_libcpp = false,
        }),
    });
    codegen.root_module.addCSourceFiles(.{
        .files = try codegen_sources_plat.toOwnedSlice(b.allocator),
        .flags = c_flags_strict,
        .language = .c,
    });
    codegen.root_module.addCSourceFiles(.{
        .files = &.{"3rdparty/stb_sprintf.c"},
        .flags = c_flags_lenient,
        .language = .c,
    });
    codegen.root_module.linkLibrary(sdl.artifact("SDL3"));
    codegen.root_module.linkLibrary(simdutf);
    return codegen;
}

fn getXxdStep(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
) !*std.Build.Step.Compile {
    const xxd_sources = &.{"3rdparty/xxd.c"};

    const xxd = b.addExecutable(.{
        .name = "xxd",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = .ReleaseFast,
            .strip = false,
            .link_libc = true,
            .link_libcpp = false,
        }),
    });
    xxd.root_module.addCSourceFiles(.{
        .files = xxd_sources,
        .flags = c_flags_strict,
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

fn addShadercrossDep(
    b: *std.Build,
    mintybreeze: *std.Build.Step.Compile,
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
    addXxdDep(b, mintybreeze, xxd, compiled_shader_path, embed_var_name);

    // Output compiled shader for inspection
    const shader_install = b.addInstallFile(compiled_shader_path, out_path);
    mintybreeze.step.dependOn(&shader_install.step);
}

// TODO avoid system xxd dependency
fn addXxdDep(
    b: *std.Build,
    mintybreeze: *std.Build.Step.Compile,
    xxd: *std.Build.Step.Compile,
    source: std.Build.LazyPath,
    var_name: []const u8,
) void {
    const run_xxd = b.addRunArtifact(xxd);
    run_xxd.addArgs(&.{ "-n", var_name, "-i" });
    run_xxd.addFileArg(source);
    const output = run_xxd.captureStdOut(.{});

    mintybreeze.root_module.addCSourceFile(.{
        .file = output,
        .language = .c,
        .flags = c_flags_strict,
    });
}

fn getPlatformSources(target: std.Build.ResolvedTarget) []const []const u8 {
    return switch (target.result.os.tag) {
        .macos => &.{ "src/platform_posix.c", "src/platform_macos.c" },
        .linux => &.{ "src/platform_posix.c", "src/platform_linux.c" },
        .windows => &.{"src/platform_windows.c"},
        else => @panic("Unsupported OS"),
    };
}

fn buildMainTarget(
    b: *std.Build,
    name: []const u8,
    sources: []const []const u8,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    codegen: *std.Build.Step.Compile,
    xxd: *std.Build.Step.Compile,
) !*std.Build.Step.Compile {
    const sdl = b.dependency("sdl", .{ .optimize = optimize, .target = target });

    const freetype = b.dependency("freetype", .{ .optimize = optimize, .target = target });

    const simdutf = getSimdutfLibrary(b, target, optimize);
    var platform_sources: std.ArrayList([]const u8) = .empty;
    try platform_sources.appendSlice(b.allocator, sources);
    try platform_sources.appendSlice(b.allocator, getPlatformSources(target));

    const main = b.addExecutable(.{
        .name = name,
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .strip = false,
            .link_libc = false, // Does SDL pull it in anyway?
            .link_libcpp = false,
        }),
    });
    main.root_module.addCSourceFiles(.{
        .files = try platform_sources.toOwnedSlice(b.allocator),
        .flags = c_flags_strict,
        .language = .c,
    });
    main.root_module.addCSourceFiles(.{
        .files = thirdparty_c_sources,
        .flags = c_flags_lenient,
        .language = .c,
    });
    main.root_module.linkLibrary(simdutf);
    main.root_module.linkLibrary(sdl.artifact("SDL3"));
    main.root_module.linkLibrary(freetype.artifact("freetype"));

    // Make mintybreeze depend on codegen
    const codegen_step = b.addRunArtifact(codegen);
    for (sources) |source| {
        if (std.mem.endsWith(u8, source, "base.c")) continue;
        codegen_step.addFileArg(b.path(source));
        codegen_step.addFileInput(b.path(source));
        // Zig build system issue IMO: you really shouldn't need to call addWatchInput() on
        // dependency graph inputs
        try codegen_step.step.addWatchInput(b.path(source));
    }
    main.step.dependOn(&codegen_step.step);

    const shader_target: ShaderTarget = switch (target.result.os.tag) {
        .macos => .msl,
        .linux => .spirv,
        .windows => .dxil,
        else => @panic("Unsupported OS"),
    };
    try addShadercrossDep(b, main, xxd, "src/shaders/vert.hlsl", .vertex, shader_target);
    try addShadercrossDep(b, main, xxd, "src/shaders/frag_icon.hlsl", .fragment, shader_target);
    try addShadercrossDep(b, main, xxd, "src/shaders/frag_glyph.hlsl", .fragment, shader_target);

    return main;
}

pub fn build(b: *std.Build) !void {
    const host_target = b.resolveTargetQuery(.{ .cpu_model = .native });
    const specified_target = b.standardTargetOptions(.{});
    const specified_optimize = b.standardOptimizeOption(.{});

    //
    // codegen
    //

    const codegen = try getCodegenStep(b, host_target);
    b.installArtifact(codegen);

    const xxd = try getXxdStep(b, host_target);
    b.installArtifact(xxd);

    //
    // mintybreeze
    //

    const mintybreeze_sources = try getSourceFiles(b, "src", ".c", "test.c");
    const mintybreeze = try buildMainTarget(
        b,
        "mintybreeze",
        mintybreeze_sources,
        specified_target,
        specified_optimize,
        codegen,
        xxd,
    );
    b.installArtifact(mintybreeze);

    const run_mintybreeze = b.addRunArtifact(mintybreeze);
    if (b.args) |args| {
        run_mintybreeze.addArgs(args);
    }
    const run_mintybreeze_step = b.step("run", "Run the application");
    run_mintybreeze_step.dependOn(&run_mintybreeze.step);

    //
    // Tests
    //

    const test_sources = try getSourceFiles(b, "src", ".c", "main.c");
    const test_exe = try buildMainTarget(
        b,
        "mintybreeze-tests",
        test_sources,
        host_target,
        specified_optimize,
        codegen,
        xxd,
    );
    b.installArtifact(test_exe);

    const run_test = b.addRunArtifact(test_exe);
    const run_test_step = b.step("test", "Run the test suite");
    run_test_step.dependOn(&run_test.step);

    //
    // Compilation database generation
    //

    var cdb_targets: std.ArrayList(*std.Build.Step.Compile) = .empty;
    try cdb_targets.append(b.allocator, mintybreeze);
    try cdb_targets.append(b.allocator, codegen);
    try cdb_targets.append(b.allocator, xxd);
    const cdb_step = zcc.createStep(b, "cdb", try cdb_targets.toOwnedSlice(b.allocator));
    // Ideally this should depend on every target it's generating the cdb for I suppose
    cdb_step.dependOn(&mintybreeze.step);
}
