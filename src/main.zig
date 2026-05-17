const std = @import("std");
const Io = std.Io;
const lexer = @import("./compiler/lexer.zig");
const parser = @import("./compiler/parser.zig").Parser;
const Compiler = @import("./compiler/compiler.zig").Compiler;
const VM = @import("./vm/vm.zig").VM;

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const arena_allocator = arena.allocator();

    const args = try init.minimal.args.toSlice(arena_allocator);

    if (args.len < 2) {
        std.debug.print("Usage: {s} <file.orus>\n", .{args[0]});
        return;
    }

    const filename = args[1];

    // Files reading
    const source = std.Io.Dir.cwd().readFileAlloc(
        init.io,
        filename,
        arena_allocator,
        .unlimited,
    ) catch |err| {
        switch (err) {
            error.FileNotFound => {
                std.debug.print("Error: cannot find file '{s}'\n", .{filename});
                return;
            },
            else => {
                std.debug.print("Erreur lecture: {}\n", .{err});
                return;
            },
        }
    };

    const tokens = lexer.tokenize(source, arena_allocator) catch |err| {
        std.debug.print("Erreur lexer: {}\n", .{err});
        return;
    };

    // for (tokens) |token| {
    //     lexer.printToken(token);
    // }

    var p = parser.init(tokens, arena_allocator, source);
    const stmts = try p.parse();

    var compiler = Compiler.init(arena_allocator);
    try compiler.compile(stmts);

    var vm = VM.init(&compiler.chunk, arena_allocator);
    defer vm.deinit();
    try vm.run();

    // std.debug.print("Prsed {} statements\n", .{stmts.len});

    // lexer.debugTokens(source);

    

}
