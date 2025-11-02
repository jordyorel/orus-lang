const std = @import("std");

const DispatchMode = enum {
    auto,
    goto,
    @"switch",
};

const BuildConfig = struct {
    description: []const u8,
    extra_cflags: []const []const u8,
    extra_defines: []const []const u8,
};

const StringList = std.ArrayList([]const u8);

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = std.builtin.OptimizeMode.ReleaseFast;

    const strict_jit = b.option(bool, "strict-jit", "Enforce JIT benchmark uplift/coverage thresholds") orelse false;
    const build_cfg = defaultBuildConfig();

    const dispatch_mode = b.option(DispatchMode, "dispatch-mode", "Dispatch selection: auto, goto, switch") orelse .auto;
    const git_commit = tryTrim(b, &.{ "git", "rev-parse", "--short", "HEAD" }) orelse "unknown";
    const git_commit_date_raw = tryTrim(b, &.{ "git", "show", "-s", "--format=%cs", "HEAD" });
    const build_date = tryTrim(b, &.{ "date", "-u", "+%Y-%m-%d" }) orelse "unknown";
    const git_commit_date = git_commit_date_raw orelse build_date;

    const portable_default = determinePortableDefault(target);
    const portable = b.option(bool, "portable", "Build with portable architecture flags") orelse portable_default;

    const allocator = b.allocator;

    const os_tag = target.result.os.tag;

    const cflags_slice = makeCoreCFlags(
        allocator,
        target,
        portable,
        dispatch_mode,
        build_cfg,
        git_commit,
        git_commit_date,
    ) catch unreachable;

    const orus_sources = buildOrusSourceList(allocator) catch unreachable;
    const orus_name: []const u8 = "orus";
    const orus_module = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    orus_module.addIncludePath(b.path("include"));
    orus_module.addIncludePath(b.path("third_party/dynasm"));
    orus_module.addCSourceFiles(.{ .files = orus_sources, .flags = cflags_slice });

    const orus_exe = b.addExecutable(.{
        .name = orus_name,
        .root_module = orus_module,
    });
    orus_exe.linkLibC();
    addCommonLibraries(orus_exe, os_tag);

    b.installArtifact(orus_exe);
    const orus_workspace_copy = addWorkspaceCopyStep(b, orus_exe, orus_name);
    maybeAttachSystemInstall(b, orus_exe, os_tag);

    const should_sign = target.result.os.tag == .macos and target.result.cpu.arch == .aarch64;
    const orus_sign_step = if (should_sign)
        maybeAddSigningStep(b, orus_exe.getEmittedBin(), &orus_exe.step, true) catch unreachable
    else
        null;

    const default_step = b.step("orus", "Build the Orus interpreter");
    default_step.dependOn(&orus_exe.step);
    default_step.dependOn(orus_workspace_copy);
    if (orus_sign_step) |step| default_step.dependOn(step);
    b.default_step.dependOn(default_step);

    const orus_install_step: *std.Build.Step = orus_workspace_copy;

    const test_step = b.step("test", "Prepare Orus binaries for downstream test runners");
    test_step.dependOn(default_step);

    const orus_bin_arg = b.fmt("./{s}", .{orus_name});
    const run_orus_tests = b.addSystemCommand(&.{
        "bash",
        "scripts/run_orus_tests.sh",
        orus_bin_arg,
        "tests",
    });
    run_orus_tests.step.dependOn(orus_install_step);
    if (orus_sign_step) |step| run_orus_tests.step.dependOn(step);
    test_step.dependOn(&run_orus_tests.step);

    const benchmark_step = b.step("jit-benchmark", "Run JIT uplift benchmarks");
    benchmark_step.dependOn(default_step);

    const benchmark_cmd = b.addSystemCommand(&.{
        "python3",
        "scripts/check_jit_benchmark.py",
        "--binary",
        orus_bin_arg,
        "--speedup",
        if (strict_jit) "3.0" else "0.0",
        "--coverage",
        if (strict_jit) "0.90" else "0.0",
        "tests/benchmarks/optimized_loop_benchmark.orus",
        "tests/benchmarks/ffi_ping_pong_benchmark.orus",
    });
    benchmark_cmd.step.dependOn(orus_install_step);
    if (orus_sign_step) |step| benchmark_cmd.step.dependOn(step);
    benchmark_step.dependOn(&benchmark_cmd.step);

    const clean_step = b.step("clean", "Remove Orus binaries and Zig caches");
    const clean_script =
        "import pathlib, shutil\n" ++
        "paths = ['orus', 'zig-out', '.zig-cache']\n" ++
        "for p in paths:\n" ++
        "    path = pathlib.Path(p)\n" ++
        "    if path.is_dir():\n" ++
        "        shutil.rmtree(path, ignore_errors=True)\n" ++
        "    else:\n" ++
        "        path.unlink(missing_ok=True)\n";
    const clean_cmd = b.addSystemCommand(&.{ "python3", "-c", clean_script });
    clean_step.dependOn(&clean_cmd.step);

    const benchmarks_step = b.step("benchmarks", "Run interpreter benchmark suite");
    benchmarks_step.dependOn(default_step);

    const unified_cmd = b.addSystemCommand(&.{
        "bash",
        "tests/benchmarks/unified_benchmark.sh",
    });
    unified_cmd.setEnvironmentVariable("ORUS_SKIP_BUILD", "1");
    unified_cmd.step.dependOn(orus_install_step);
    if (orus_sign_step) |step| unified_cmd.step.dependOn(step);
    benchmarks_step.dependOn(&unified_cmd.step);
}

fn addWorkspaceCopyStep(b: *std.Build, artifact: *std.Build.Step.Compile, dest_filename: []const u8) *std.Build.Step {
    const dest_path = b.build_root.join(b.allocator, &.{dest_filename}) catch unreachable;
    return addCopyStep(b, artifact, dest_path);
}

fn addCopyStep(b: *std.Build, artifact: *std.Build.Step.Compile, dest_path: []const u8) *std.Build.Step {
    const copy_script =
        "import os, shutil, sys\n" ++
        "src = sys.argv[1]\n" ++
        "dst = sys.argv[2]\n" ++
        "parent = os.path.dirname(dst)\n" ++
        "if parent:\n" ++
        "    os.makedirs(parent, exist_ok=True)\n" ++
        "shutil.copy2(src, dst)\n";
    const copy_cmd = b.addSystemCommand(&.{ "python3", "-c", copy_script });
    copy_cmd.addFileArg(artifact.getEmittedBin());
    copy_cmd.addArg(dest_path);
    copy_cmd.step.dependOn(&artifact.step);
    return &copy_cmd.step;
}

fn maybeAttachSystemInstall(b: *std.Build, artifact: *std.Build.Step.Compile, os_tag: std.Target.Os.Tag) void {
    if (!shouldMirrorSystemInstall(b, os_tag)) return;
    const bin_dir = systemInstallBinDir(os_tag) orelse return;
    const dest_path = b.fmt("{s}/{s}", .{ bin_dir, artifact.out_filename });
    const system_step = addCopyStep(b, artifact, dest_path);
    b.getInstallStep().dependOn(system_step);
}

fn shouldMirrorSystemInstall(b: *std.Build, os_tag: std.Target.Os.Tag) bool {
    if (b.dest_dir != null) return false;
    const zig_out_default = b.build_root.join(b.allocator, &.{"zig-out"}) catch unreachable;
    defer b.allocator.free(zig_out_default);
    if (!std.mem.eql(u8, b.install_prefix, zig_out_default)) return false;
    return hasInstallPrivileges(os_tag);
}

fn hasInstallPrivileges(os_tag: std.Target.Os.Tag) bool {
    return switch (os_tag) {
        .windows => false,
        else => std.posix.geteuid() == 0,
    };
}

fn systemInstallBinDir(os_tag: std.Target.Os.Tag) ?[]const u8 {
    return switch (os_tag) {
        .macos => "/Library/Orus/bin",
        .linux => "/usr/local/lib/orus/bin",
        .windows => "C:/Program Files/Orus/bin",
        else => null,
    };
}

fn defaultBuildConfig() BuildConfig {
    return .{
        .description = "Release (maximum optimization)",
        .extra_cflags = &.{
            "-O3",
            "-g1",
            "-DNDEBUG=1",
            "-funroll-loops",
            "-finline-functions",
        },
        .extra_defines = &.{
            "-DRELEASE_MODE=1",
            "-DENABLE_OPTIMIZATIONS=1",
        },
    };
}

fn makeCoreCFlags(
    allocator: std.mem.Allocator,
    target: std.Build.ResolvedTarget,
    portable: bool,
    dispatch_mode: DispatchMode,
    build_cfg: BuildConfig,
    git_commit: []const u8,
    git_commit_date: []const u8,
) ![]const []const u8 {
    var cflags = StringList.empty;
    try addBaseCompilerFlags(&cflags, allocator);
    try addConfigFlags(&cflags, allocator, build_cfg.extra_cflags);
    try addPortableFlags(&cflags, allocator, target, portable);
    try addDispatchDefine(&cflags, allocator, dispatch_mode, target);
    try addGitDefines(&cflags, allocator, git_commit, git_commit_date);

    const os_tag = target.result.os.tag;
    if (os_tag == .linux or os_tag == .macos) {
        try addFlag(&cflags, allocator, "-pthread");
    }

    try addDefines(&cflags, allocator, build_cfg.extra_defines);
    return cflags.toOwnedSlice(allocator);
}

fn addBaseCompilerFlags(cflags: *StringList, allocator: std.mem.Allocator) !void {
    try cflags.appendSlice(allocator, &.{
        "-Wall",
        "-Wextra",
        "-std=c11",
    });
}

fn addConfigFlags(cflags: *StringList, allocator: std.mem.Allocator, extra: []const []const u8) !void {
    try cflags.appendSlice(allocator, extra);
}

fn addDefines(cflags: *StringList, allocator: std.mem.Allocator, defines: []const []const u8) !void {
    try cflags.appendSlice(allocator, defines);
}

fn addFlag(cflags: *StringList, allocator: std.mem.Allocator, flag: []const u8) !void {
    try cflags.append(allocator, flag);
}

fn addPortableFlags(cflags: *StringList, allocator: std.mem.Allocator, target: std.Build.ResolvedTarget, portable: bool) !void {
    const os_tag = target.result.os.tag;
    const arch = target.result.cpu.arch;
    if (portable) {
        if (os_tag == .macos) {
            switch (arch) {
                .aarch64 => try cflags.appendSlice(allocator, &.{ "-arch", "arm64" }),
                .x86_64 => try cflags.appendSlice(allocator, &.{ "-arch", "x86_64" }),
                else => {},
            }
        }
    } else {
        switch (arch) {
            .aarch64 => {
                if (os_tag == .macos) {
                    try cflags.appendSlice(allocator, &.{
                        "-mcpu=apple-m1",
                        "-mtune=apple-m1",
                        "-march=armv8.4-a+simd+crypto+sha3",
                    });
                }
            },
            .x86_64 => try cflags.appendSlice(allocator, &.{ "-march=native", "-mtune=native" }),
            else => {},
        }
    }
}

fn addDispatchDefine(cflags: *StringList, allocator: std.mem.Allocator, mode: DispatchMode, target: std.Build.ResolvedTarget) !void {
    const define = switch (mode) {
        .goto => "-DUSE_COMPUTED_GOTO=1",
        .@"switch" => "-DUSE_COMPUTED_GOTO=0",
        .auto => blk: {
            const os_tag = target.result.os.tag;
            const arch = target.result.cpu.arch;
            if (os_tag == .linux) {
                if (arch == .aarch64 or arch == .arm) {
                    break :blk "-DUSE_COMPUTED_GOTO=0";
                } else {
                    break :blk "-DUSE_COMPUTED_GOTO=1";
                }
            } else if (os_tag == .macos) {
                break :blk "-DUSE_COMPUTED_GOTO=1";
            } else {
                break :blk "-DUSE_COMPUTED_GOTO=0";
            }
        },
    };

    try cflags.append(allocator, define);
}

fn addGitDefines(
    cflags: *StringList,
    allocator: std.mem.Allocator,
    commit: []const u8,
    commit_date: []const u8,
) !void {
    const commit_define = try std.fmt.allocPrint(allocator, "-DORUS_BUILD_COMMIT=\"{s}\"", .{commit});
    const date_define = try std.fmt.allocPrint(allocator, "-DORUS_BUILD_DATE=\"{s}\"", .{commit_date});
    try cflags.append(allocator, commit_define);
    try cflags.append(allocator, date_define);
}

fn tryTrim(b: *std.Build, args: []const []const u8) ?[]const u8 {
    var exit_code: u8 = 0;
    const stdout = b.runAllowFail(args, &exit_code, .Inherit) catch return null;
    defer b.allocator.free(stdout);

    if (exit_code != 0) return null;

    const trimmed = std.mem.trim(u8, stdout, " \n\r\t");
    if (trimmed.len == 0) return null;
    return b.dupe(trimmed);
}

fn determinePortableDefault(target: std.Build.ResolvedTarget) bool {
    return target.result.os.tag == .macos and target.result.cpu.arch == .aarch64;
}

fn buildOrusSourceList(allocator: std.mem.Allocator) ![]const []const u8 {
    var list = StringList.empty;
    try appendSources(&list, allocator, compilerFrontendSources);
    try appendSources(&list, allocator, compilerBackendSources);
    try appendSources(&list, allocator, compilerExtraSources);
    try appendSources(&list, allocator, vmSources);
    try appendSources(&list, allocator, replSources);
    try appendSources(&list, allocator, mainSources);
    return list.toOwnedSlice(allocator);
}

fn appendSources(list: *StringList, allocator: std.mem.Allocator, sources: []const []const u8) !void {
    try list.appendSlice(allocator, sources);
}

fn maybeAddSigningStep(
    b: *std.Build,
    artifact: std.Build.LazyPath,
    dependency: *std.Build.Step,
    attach_to_install: bool,
) !?*std.Build.Step {
    const sign_helper = "scripts/macos/sign-with-jit.sh";
    std.fs.cwd().access(sign_helper, .{}) catch return null;

    const sign_cmd = b.fmt("{s} \"$@\" || true", .{sign_helper});
    const sign = b.addSystemCommand(&.{ "bash", "-c", sign_cmd, "orus-sign" });
    sign.addFileArg(artifact);
    sign.step.dependOn(dependency);
    if (attach_to_install) {
        b.getInstallStep().dependOn(&sign.step);
    }
    return &sign.step;
}

fn addCommonLibraries(exe: *std.Build.Step.Compile, os_tag: std.Target.Os.Tag) void {
    switch (os_tag) {
        .linux, .macos => {
            exe.linkSystemLibrary("pthread");
            exe.linkSystemLibrary("m");
        },
        .windows => {},
        else => {
            exe.linkSystemLibrary("m");
        },
    }
}

const compilerFrontendSources = &.{
    "src/compiler/frontend/lexer.c",
    "src/compiler/frontend/parser.c",
};

const compilerBackendSources = &.{
    "src/compiler/backend/typed_ast_visualizer.c",
    "src/compiler/backend/register_allocator.c",
    "src/compiler/backend/compiler.c",
    "src/compiler/backend/optimization/optimizer.c",
    "src/compiler/backend/optimization/loop_type_affinity.c",
    "src/compiler/backend/optimization/loop_type_residency.c",
    "src/compiler/backend/optimization/constantfold.c",
    "src/compiler/backend/codegen/codegen.c",
    "src/compiler/backend/codegen/expressions.c",
    "src/compiler/backend/codegen/statements.c",
    "src/compiler/backend/codegen/functions.c",
    "src/compiler/backend/codegen/modules.c",
    "src/compiler/backend/codegen/peephole.c",
    "src/compiler/backend/error_reporter.c",
    "src/compiler/backend/scope_stack.c",
    "src/compiler/backend/specialization/profiling_feedback.c",
    "src/compiler/symbol_table.c",
};

const compilerExtraSources = &.{
    "src/compiler/typed_ast.c",
    "src/debug/debug_config.c",
};

const vmSources = &.{
    "src/vm/core/vm_core.c",
    "src/vm/core/vm_tagged_union.c",
    "src/vm/runtime/vm.c",
    "src/vm/core/vm_memory.c",
    "src/vm/utils/debug.c",
    "src/vm/runtime/builtin_print.c",
    "src/vm/runtime/builtin_input.c",
    "src/vm/runtime/builtin_array_push.c",
    "src/vm/runtime/builtin_array_pop.c",
    "src/vm/runtime/builtin_array_repeat.c",
    "src/vm/runtime/builtin_timestamp.c",
    "src/vm/runtime/builtin_number.c",
    "src/vm/runtime/builtin_typeof.c",
    "src/vm/runtime/builtin_istype.c",
    "src/vm/runtime/builtin_range.c",
    "src/vm/runtime/builtin_sorted.c",
    "src/vm/runtime/builtin_assert.c",
    "src/vm/runtime/jit_benchmark.c",
    "src/vm/operations/vm_arithmetic.c",
    "src/vm/operations/vm_control_flow.c",
    "src/vm/operations/vm_typed_ops.c",
    "src/vm/operations/vm_string_ops.c",
    "src/vm/operations/vm_comparison.c",
    "src/vm/handlers/vm_arithmetic_handlers.c",
    "src/vm/handlers/vm_control_flow_handlers.c",
    "src/vm/handlers/vm_memory_handlers.c",
    "src/vm/dispatch/vm_dispatch_switch.c",
    "src/vm/dispatch/vm_dispatch_goto.c",
    "src/vm/core/vm_validation.c",
    "src/vm/register_file.c",
    "src/vm/spill_manager.c",
    "src/vm/module_manager.c",
    "src/vm/register_cache.c",
    "src/vm/profiling/vm_profiling.c",
    "src/vm/runtime/vm_tiering.c",
    "src/vm/vm_config.c",
    "src/vm/jit/orus_jit_backend.c",
    "src/vm/jit/orus_jit_debug.c",
    "src/vm/jit/orus_jit_ir.c",
    "src/vm/jit/orus_jit_ir_debug.c",
    "src/type/type_representation.c",
    "src/type/type_inference.c",
    "src/errors/infrastructure/error_infrastructure.c",
    "src/errors/core/error_base.c",
    "src/errors/features/type_errors.c",
    "src/errors/features/variable_errors.c",
    "src/errors/features/control_flow_errors.c",
    "src/config/config.c",
    "src/internal/logging.c",
};

const replSources = &.{
    "src/repl.c",
};

const mainSources = &.{
    "src/main.c",
};
