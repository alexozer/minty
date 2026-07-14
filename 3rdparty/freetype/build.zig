const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const use_system_zlib = b.option(bool, "use_system_zlib", "Use system zlib") orelse false;

    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    const lib = b.addLibrary(.{
        .name = "freetype",
        .root_module = mod,
    });
    mod.addIncludePath(b.path("include"));
    mod.addCMacro("FT2_BUILD_LIBRARY", "1");

    if (use_system_zlib) {
        mod.addCMacro("FT_CONFIG_OPTION_SYSTEM_ZLIB", "1");
    }

    mod.addCMacro("HAVE_UNISTD_H", "1");
    mod.addCSourceFiles(.{ .files = &sources, .flags = &.{} });
    if (target.result.os.tag == .macos) mod.addCSourceFile(.{
        .file = b.path("src/base/ftmac.c"),
        .flags = &.{},
    });

    // iOS targets require the iOS SDK sysroot (libc headers are not bundled
    // with Zig). Pull the headers, frameworks, and stub libs from the bundled
    // `xcode_frameworks` package so the C sources can find <string.h> etc.
    if (target.result.os.tag == .ios) {
        if (b.lazyDependency("xcode_frameworks", .{
            .target = target,
            .optimize = optimize,
        })) |dep| {
            const subdir = if (target.result.abi == .simulator) "iphonesimulator" else "iphoneos";
            mod.addSystemFrameworkPath(dep.path(b.fmt("{s}/Frameworks", .{subdir})));
            mod.addSystemIncludePath(dep.path(b.fmt("{s}/include", .{subdir})));
            mod.addLibraryPath(dep.path(b.fmt("{s}/lib", .{subdir})));
        }
    }
    lib.installHeadersDirectory(b.path("include/freetype"), "freetype", .{});
    lib.installHeader(b.path("include/ft2build.h"), "ft2build.h");
    b.installArtifact(lib);
}

const sources = [_][]const u8{
    "src/autofit/autofit.c",
    "src/base/ftbase.c",
    "src/base/ftsystem.c",
    "src/base/ftdebug.c",
    "src/base/ftbbox.c",
    "src/base/ftbdf.c",
    "src/base/ftbitmap.c",
    "src/base/ftcid.c",
    "src/base/ftfstype.c",
    "src/base/ftgasp.c",
    "src/base/ftglyph.c",
    "src/base/ftgxval.c",
    "src/base/ftinit.c",
    "src/base/ftmm.c",
    "src/base/ftotval.c",
    "src/base/ftpatent.c",
    "src/base/ftpfr.c",
    "src/base/ftstroke.c",
    "src/base/ftsynth.c",
    "src/base/fttype1.c",
    "src/base/ftwinfnt.c",
    "src/bdf/bdf.c",
    "src/bzip2/ftbzip2.c",
    "src/cache/ftcache.c",
    "src/cff/cff.c",
    "src/cid/type1cid.c",
    "src/gzip/ftgzip.c",
    "src/lzw/ftlzw.c",
    "src/pcf/pcf.c",
    "src/pfr/pfr.c",
    "src/psaux/psaux.c",
    "src/pshinter/pshinter.c",
    "src/psnames/psnames.c",
    "src/raster/raster.c",
    "src/sdf/sdf.c",
    "src/sfnt/sfnt.c",
    "src/smooth/smooth.c",
    "src/svg/svg.c",
    "src/truetype/truetype.c",
    "src/type1/type1.c",
    "src/type42/type42.c",
    "src/hvf/hvf.c",
    "src/winfonts/winfnt.c",
};
