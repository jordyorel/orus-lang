const std = @import("std");

// ---- ObjType ----
pub const ObjType = enum {
    String,
};

// ---- Obj ----
// Header commun à tous les objets heap.
// Chaque objet commence par ce header — ça permet de caster entre types.

pub const Obj = struct {
    type: ObjType,
    next: ?*Obj, // liste chaînée de tous les objets — pour le GC plus tard
};

// ---- ObjString ----
// Une string Orus est :
//   - un header Obj
//   - un hash pré-calculé (FNV-1a 32 bits)
//   - les bytes de la string
//
// Le hash est calculé une seule fois à la création.
// Les lookups dans la table des globals sont O(1).

pub const ObjString = struct {
    obj: Obj, // doit être le premier champ — permet le cast Obj <-> ObjString
    hash: u32,
    bytes: []const u8,

    // Alloue et initialise un ObjString
    pub fn create(allocator: std.mem.Allocator, bytes: []const u8) !*ObjString {
        const s = try allocator.create(ObjString);
        s.* = .{
            .obj = .{ .type = .String, .next = null },
            .hash = hashBytes(bytes),
            .bytes = bytes,
        };
        return s;
    }

    pub fn destroy(self: *ObjString, allocator: std.mem.Allocator) void {
        allocator.destroy(self);
    }
};

// ─── Hash FNV-1a 32 bits ─────────────────────────────────────────────────────
// Simple, rapide, bonne distribution pour des identifiants courts.
// C'est ce que Lua et clox utilisent.

pub fn hashBytes(bytes: []const u8) u32 {
    var h: u32 = 2166136261; // FNV offset basis
    for (bytes) |b| {
        h ^= b;
        h *%= 16777619; // FNV prime — *%= pour wrapping mul
    }
    return h;
}
