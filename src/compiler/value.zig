const std = @import("std");
const ObjString = @import("object.zig").ObjString;

const QNAN: u64 = 0x7FFC_0000_0000_0000;

// Non-Numeric Tag
const TAG_NIL: u64 = 0x0000_0000_0000_0001;
const TAG_FALSE: u64 = 0x0000_0000_0000_0002;
const TAG_TRUE: u64 = 0x0000_0000_0000_0003;

// Heap object Tags
const TAG_OBJ: u64 = 0x8000_0000_0000_0000;

// Type Value
// A value always has 8 bytes
pub const Value = u64;

// Constants
pub const NIL: Value = QNAN | TAG_NIL;
pub const FALSE: Value = QNAN | TAG_FALSE;
pub const TRUE: Value = QNAN | TAG_TRUE;

// ----Encoders----
pub fn fromNumber(n: f64) Value {
    return @bitCast(n);
}

pub fn fromObj(ptr: *anyopaque) Value {
    return QNAN | TAG_OBJ | @intFromPtr(ptr);
}

pub fn fromBool(b: bool) Value {
    return if (b) TRUE else FALSE;
}

pub fn fromString(s: *ObjString) Value {
    return fromObj(@ptrCast(s));
}

// ----Decoders----
pub fn toNumber(v: Value) f64 {
    return @bitCast(v);
}

pub fn toObj(v: Value) *anyopaque {
    return @ptrFromInt(v & ~(QNAN | TAG_OBJ));
}

pub fn toBool(v: Value) bool {
    return v == TRUE;
}

pub fn toString(v: Value) *ObjString {
    return @ptrCast(@alignCast(toObj(v)));
}

// ----Predicats----
pub fn isNumber(v: Value) bool {
    return (v & QNAN) != QNAN;
}

pub fn isNil(v: Value) bool {
    return v == NIL;
}

pub fn isBool(v: Value) bool {
    return v == TRUE or v == FALSE;
}

pub fn isObj(v: Value) bool {
    return (v & (QNAN | TAG_OBJ)) == (QNAN | TAG_OBJ);
}

pub fn isString(v: Value) bool {
    if (!isObj(v)) return false;
    return toString(v).obj.type == .String;
}

// --- Helpers ---
pub fn isFalsy(v: Value) bool {
    return v == FALSE or v == NIL;
}

pub fn valuesEqual(a: Value, b: Value) bool {
    return a == b;
}

// Display
pub fn print(v: Value) void {
    if (isNumber(v)) {
        std.debug.print("{d}", .{toNumber(v)});
    } else if (isNil(v)) {
        std.debug.print("nil", .{});
    } else if (isBool(v)) {
        std.debug.print("{s}", .{if (toBool(v)) "true" else "false"});
    } else if (isObj(v)) {
        const s = toString(v);
        std.debug.print("{s}", .{s.bytes});
    }
}
