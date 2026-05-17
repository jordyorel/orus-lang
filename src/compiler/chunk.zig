const std = @import("std");
const Value = @import("value.zig").Value;
const StringTable = @import("string_table.zig").StringTable;
const ObjString = @import("object.zig").ObjString;

pub const Opcode = enum {
    Loadk,
    LoadBool,
    LoadNil,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Negate,
    Not,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    SetGlobal,
    GetGlobal,
    Print,
    PrintInline,
};

pub const Chunk = struct {
    code: std.ArrayList(u32),
    constants: std.ArrayList(Value),
    strings: std.ArrayList(*ObjString),
    string_table: StringTable,
    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator) Chunk {
        return .{
            .code = std.ArrayList(u32).empty,
            .constants = std.ArrayList(Value).empty,
            .allocator = allocator,
            .strings = std.ArrayList(*ObjString).empty,
            .string_table = StringTable.init(allocator),
        };
    }

    pub fn deinit(self: *Chunk) void {
        self.code.deinit(self.allocator);
        self.constants.deinit(self.allocator);
    }

    pub fn writeInstruction(self: *Chunk, op: Opcode, dst: u8, b: u8, c: u8) !void {
        try self.code.append(self.allocator, encode(op, dst, b, c));
    }

    pub fn encode(op: Opcode, dst: u8, b: u8, c: u8) u32 {
        return @as(u32, @intFromEnum(op)) << 24 | (@as(u32, dst) << 16) | (@as(u32, b) << 8) | (@as(u32, c));
    }

    pub fn getOp(i: u32) Opcode {
        return @enumFromInt(@as(u8, @truncate(i >> 24)));
    }

    pub fn getDest(i: u32) u8 {
        return @truncate(i >> 16);
    }

    pub fn getB(i: u32) u8 {
        return @truncate(i >> 8);
    }

    pub fn getC(i: u32) u8 {
        return @truncate(i);
    }

    pub fn addConstant(self: *Chunk, value: Value) !u8 {
        try self.constants.append(self.allocator, value);
        return @intCast(self.constants.items.len - 1);
    }

    pub fn addString(self: *Chunk, bytes: [] const u8) !u8 {
        const s = try self.string_table.intern(self.allocator, bytes);
        for (self.strings.items, 0..) |existing, i| {
            if (existing == s) return @intCast(i);
        }
        try self.strings.append(self.allocator, s);
        return @intCast(self.strings.items.len - 1);
    }
};
