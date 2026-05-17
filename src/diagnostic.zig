const std = @import("std");
const Token = @import("compiler/lexer.zig").Token;

pub fn getSourceLine(source: []const u8, token: Token) []const u8 {
    // trouve le début de la ligne
    const token_idx = @intFromPtr(token.start) - @intFromPtr(source.ptr);
    // trouve la fin de la ligne
    var line_start = token_idx;
    while (line_start > 0 and source[line_start - 1] != '\n') {
        line_start -= 1;
    }

    var line_end = token_idx;
    while (line_end < source.len and source[line_end] != '\n') {
        line_end += 1;
    }
    // retourne le slice
    return source[line_start..line_end];
}

pub fn report(source: []const u8, token: Token, msg: []const u8) void {
    // affiche [line X, col Y] Error: msg
    std.debug.print("[line {d}, col {d}] Error: {s}\n", .{
        token.line,
        token.column,
        msg,
    });

    // affiche la ligne source
    const line_slice = getSourceLine(source, token);
    std.debug.print("{s}\n", .{line_slice});

    // affiche le caret ^ sous le token
    // token.column est 1-based, donc on imprime (column - 1) espaces
    var i: u32 = 0;
    while (i < token.column - 1) : (i += 1) {
        std.debug.print(" ", .{});
    }
    // puis ^ sur toute la longueur du token
    i = 0;
    while (i < token.length) : (i += 1) {
        std.debug.print("^", .{});
    }
    std.debug.print("\n", .{});
}
