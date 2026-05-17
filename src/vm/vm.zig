const std = @import("std");
const Chunk = @import("../compiler/chunk.zig").Chunk;
const value = @import("../compiler/value.zig");
const Value = value.Value;

const ObjString = @import("../compiler/object.zig").ObjString;
const NIL = @import("../compiler/value.zig").NIL;

const GlobalTable = std.AutoHashMap(*ObjString, Value);

const InterpretError = error{
    RuntimeError,
    OutOfMemory,
};

pub const VM = struct {
    chunk: *Chunk,
    ip: usize,
    registers: [256]Value,
    globals: GlobalTable,

    pub fn init(chunk_: *Chunk, allocator: std.mem.Allocator) VM {
        return .{ .chunk = chunk_, .ip = 0, .registers = [_]Value{NIL} ** 256, .globals = GlobalTable.init(allocator) };
    }

    pub fn deinit(self: *VM) void {
        self.globals.deinit();
    }

    pub fn run(self: *VM) InterpretError!void {
        while (self.ip < self.chunk.code.items.len) {
            const instruction = self.chunk.code.items[self.ip];
            self.ip += 1;

            const op = Chunk.getOp(instruction);
            const dst = Chunk.getDest(instruction);
            const b = Chunk.getB(instruction);
            const c = Chunk.getC(instruction);

            switch (op) {
                .Loadk => {
                    self.registers[dst] = self.chunk.constants.items[b];
                },

                .LoadBool => {
                    self.registers[dst] = if (b == 1) value.TRUE else value.FALSE;
                },

                .LoadNil => {
                    self.registers[dst] = value.NIL;
                },

                .Negate => {
                    const va = self.registers[b];
                    if (!value.isNumber(va)) {
                        std.debug.print("Runtime error: Negation requires a number\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    self.registers[dst] = value.fromNumber(-value.toNumber(va));
                },

                .Not => {
                    const va = self.registers[b];
                    self.registers[dst] = if (value.isFalsy(va)) value.TRUE else value.FALSE;
                },

                .SetGlobal => {
                    const name = self.decodeName(b);
                    try self.globals.put(name, self.registers[dst]);
                },

                .GetGlobal => {
                    const name = self.decodeName(b);
                    const val = self.globals.get(name) orelse {
                        std.debug.print("Runtime error: variable '{s}' non définie\n", .{name.bytes});
                        return InterpretError.RuntimeError;
                    };
                    self.registers[dst] = val;
                },

                .Add => {
                    const vb = self.registers[b];
                    const vc = self.registers[c];
                    if (!value.isNumber(vb) or !value.isNumber(vc)) {
                        std.debug.print("Runtime error: Addition requiers two numbers\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    self.registers[dst] = value.fromNumber(value.toNumber(vb) + value.toNumber(vc));
                },

                .Sub => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    if (!value.isNumber(va) or !value.isNumber(vb)) {
                        std.debug.print("Runtime error: Subustraction requiers two numbers\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    self.registers[dst] = value.fromNumber(value.toNumber(va) - value.toNumber(vb));
                },

                .Mul => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    if (!value.isNumber(va) or !value.isNumber(vb)) {
                        std.debug.print("Runtime error: Multiplication requiers two numbers\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    self.registers[dst] = value.fromNumber(value.toNumber(va) * value.toNumber(vb));
                },

                .Div => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    if (!value.isNumber(va) or !value.isNumber(vb)) {
                        std.debug.print("Runtime error: Divison requiers two numbers\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    self.registers[dst] = value.fromNumber(value.toNumber(va) / value.toNumber(vb));
                },

                .Mod => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    if (!value.isNumber(va) or !value.isNumber(vb)) {
                        std.debug.print("Runtime error: Modulo requires two numbers\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    const a_int = @as(i64, @intFromFloat(value.toNumber(va)));
                    const b_int = @as(i64, @intFromFloat(value.toNumber(vb)));
                    self.registers[dst] = value.fromNumber(@as(f64, @floatFromInt(@rem(a_int, b_int))));
                },

                .Equal => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    self.registers[dst] = if (value.valuesEqual(va, vb)) value.TRUE else value.FALSE;
                },

                .NotEqual => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    self.registers[dst] = if (!value.valuesEqual(va, vb)) value.TRUE else value.FALSE;
                },

                .Less => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    if (!value.isNumber(va) or !value.isNumber(vb)) {
                        std.debug.print("Runtime error: Comparison requires numbers\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    self.registers[dst] = if (value.toNumber(va) < value.toNumber(vb)) value.TRUE else value.FALSE;
                },

                .LessEqual => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    if (!value.isNumber(va) or !value.isNumber(vb)) {
                        std.debug.print("Runtime error: Comparison requires numbers\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    self.registers[dst] = if (value.toNumber(va) <= value.toNumber(vb)) value.TRUE else value.FALSE;
                },

                .Greater => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    if (!value.isNumber(va) or !value.isNumber(vb)) {
                        std.debug.print("Runtime error: Comparison requires numbers\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    self.registers[dst] = if (value.toNumber(va) > value.toNumber(vb)) value.TRUE else value.FALSE;
                },

                .GreaterEqual => {
                    const va = self.registers[b];
                    const vb = self.registers[c];
                    if (!value.isNumber(va) or !value.isNumber(vb)) {
                        std.debug.print("Runtime error: Comparison requires numbers\n", .{});
                        return InterpretError.RuntimeError;
                    }
                    self.registers[dst] = if (value.toNumber(va) >= value.toNumber(vb)) value.TRUE else value.FALSE;
                },

                .Print => {
                    for (0..b) |i| {
                        value.print(self.registers[dst + i]);
                        if (i < b - 1) std.debug.print(" ", .{});
                    }
                    std.debug.print("\n", .{});
                },

                .PrintInline => {
                    value.print(self.registers[dst]);
                },
            }
        }
    }

    fn decodeName(self: *VM, idx: u8) *ObjString {
        return self.chunk.strings.items[idx];
    }
};
