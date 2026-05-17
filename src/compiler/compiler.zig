const std = @import("std");
const Chunk = @import("chunk.zig").Chunk;
const Stmt = @import("parser.zig").Stmt;
const Expr = @import("parser.zig").Expr;
const Assign = @import("parser.zig").Assign;
const Value = @import("value.zig");

pub const Compiler = struct {
    chunk: Chunk,
    next_reg: u8,
    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator) Compiler {
        return .{
            .chunk = Chunk.init(allocator),
            .next_reg = 0,
            .allocator = allocator,
        };
    }

    pub fn allocReg(self: *Compiler) u8 {
        const reg = self.next_reg;
        self.next_reg += 1;
        return reg;
    }

    pub fn freeReg(self: *Compiler) void {
        self.next_reg -= 1;
    }

    pub fn deinit(self: *Compiler) void {
        self.chunk.deinit();
    }

    pub fn compile(self: *Compiler, stmts: []Stmt) !void {
        for (stmts) |stmt| try self.compileStmt(stmt);
    }

    pub fn compileStmt(self: *Compiler, stmt: Stmt) !void {
        switch (stmt) {
            .Assign => |a| try self.compileAssign(a),
            .Print => |args| try self.compilePrint(args),
        }
    }

    fn compileAssign(self: *Compiler, a: Assign) !void {
        const value_reg = try self.compileExpression(a.value);
        const name_idx = try self.chunk.addString(a.name);

        try self.chunk.writeInstruction(.SetGlobal, value_reg, name_idx, 0);
        self.freeReg();
    }

    fn compilePrint(self: *Compiler, args: []*Expr) !void {
        const first_reg = self.next_reg;
        for (args) |arg| {
            _ = try self.compileExpression(arg);
        }
        const count = args.len;
        try self.chunk.writeInstruction(.Print, first_reg, @intCast(count), 0);

        for (0..count) |_| {
            self.freeReg();
        }
    }

    fn compileExpression(self: *Compiler, expr: *Expr) !u8 {
        switch (expr.*) {
            .Number => |n| {
                const dst = self.allocReg();
                const val_idx = try self.chunk.addConstant(Value.fromNumber(n));
                try self.chunk.writeInstruction(.Loadk, dst, val_idx, 0);
                return dst;
            },

            .Bool => |b| {
                const dst = self.allocReg();
                try self.chunk.writeInstruction(.LoadBool, dst, if (b) 1 else 0, 0);
                return dst;
            },

            .Nil => {
                const dst = self.allocReg();
                try self.chunk.writeInstruction(.LoadNil, dst, 0, 0);
                return dst;
            },

            .Variable => |name| {
                const dst = self.allocReg();
                const name_idx = try self.chunk.addString(name);
                try self.chunk.writeInstruction(.GetGlobal, dst, name_idx, 0);
                return dst;
            },

            .Unary => |unary| {
                const src = try self.compileExpression(unary.right);
                switch (unary.op) {
                    .Minus => {
                        try self.chunk.writeInstruction(.Negate, src, src, 0);
                    },
                    .BitNot => {
                        try self.chunk.writeInstruction(.Not, src, src, 0);
                    },
                    else => @panic("Unary operator not supported."),
                }
                return src;
            },

            .String => |bytes| {
                const dst = self.allocReg();
                const obj = try self.chunk.string_table.intern(self.allocator, bytes);
                const val = Value.fromString(obj);
                const val_idx = try self.chunk.addConstant(val);
                try self.chunk.writeInstruction(.Loadk, dst, val_idx, 0);
                return dst;
            },

            .Binary => |bin| {
                const left = try self.compileExpression(bin.left);
                const right = try self.compileExpression(bin.right);

                self.freeReg();
                const dst = left;
                switch (bin.op) {
                    .Plus => try self.chunk.writeInstruction(.Add, dst, left, right),
                    .Minus => try self.chunk.writeInstruction(.Sub, dst, left, right),
                    .Slash => try self.chunk.writeInstruction(.Div, dst, left, right),
                    .Star => try self.chunk.writeInstruction(.Mul, dst, left, right),
                    .Modulo => try self.chunk.writeInstruction(.Mod, dst, left, right),
                    .EqualEqual => try self.chunk.writeInstruction(.Equal, dst, left, right),
                    .BangEqual => try self.chunk.writeInstruction(.NotEqual, dst, left, right),
                    .Less => try self.chunk.writeInstruction(.Less, dst, left, right),
                    .LessEqual => try self.chunk.writeInstruction(.LessEqual, dst, left, right),
                    .Greater => try self.chunk.writeInstruction(.Greater, dst, left, right),
                    .GreaterEqual => try self.chunk.writeInstruction(.GreaterEqual, dst, left, right),
                    else => @panic("Binary operator not supported"),
                }
                return dst;
            },

            // else => unreachable,
        }
    }
};
