// Table d'interning des strings.
// Deux strings avec les mêmes bytes → même pointeur ObjString.
// Ça permet la comparaison O(1) par pointeur au lieu de comparer les bytes.

const std = @import("std");
const ObjString = @import("object.zig").ObjString;
const hashBytes = @import("object.zig").hashBytes;

pub const StringTable = struct {
    map: std.StringHashMap(*ObjString),

    pub fn init(allocator: std.mem.Allocator) StringTable {
        return .{ .map = std.StringHashMap(*ObjString).init(allocator) };
    }

    pub fn deinit(self: *StringTable, allocator: std.mem.Allocator) void {
        var it = self.map.iterator();
        while (it.next()) |entry| {
            entry.value_ptr.*.destroy(allocator);
        }
        self.map.deinit();
    }

    // Retourne un ObjString interné pour ces bytes.
    // Si une string identique existe déjà → retourne le pointeur existant.
    // Sinon → alloue un nouvel ObjString et l'insère.
    pub fn intern(self: *StringTable, allocator: std.mem.Allocator, bytes: []const u8,) !*ObjString {
        if (self.map.get(bytes)) |existing| return existing;
        const s = try ObjString.create(allocator, bytes);
        try self.map.put(s.bytes, s);
        return s;
    }
};
