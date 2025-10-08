**Minimal-but-powerful** filesystem design for Orus that:

* runs on C under the hood,
* exposes a clean Orus API,
* adds **no new keywords**, and
* follows the same `@[core]` bridge you’re using for `math`.



# std/fs — Minimal Filesystem API (Orus wrappers over C)

## Design goals

* **Small surface** first: open, read, write, close, stat, list, mkdir, remove, rename.
* **Everything via `use fs`** (no global builtins).
* **Opaque handles** for files; pure functions for path ops.
* **Safe defaults** (text is UTF-8, writes are explicit, no silent truncation).
* **Cross-platform**: C core abstracts POSIX/Win differences.

---

## C core (hidden intrinsics)

```c
// core_fs.c  (symbol names registered in VM)
typedef void* orus_file_t; // opaque handle

// open/close
orus_file_t orus_fs_open(const char* path,
                         int mode_flags,    // bitmask: READ=1, WRITE=2, APPEND=4, CREATE=8, TRUNC=16
                         int perm_octal);   // e.g., 0644 (POSIX); ignored on Windows
int         orus_fs_close(orus_file_t fh);

// read/write
// returns bytes read/written or negative error code
long        orus_fs_read(orus_file_t fh, void* buf, long len);
long        orus_fs_write(orus_file_t fh, const void* buf, long len);
int         orus_fs_flush(orus_file_t fh);

// file position
long long   orus_fs_seek(orus_file_t fh, long long offset, int whence); // 0:SET 1:CUR 2:END
long long   orus_fs_tell(orus_file_t fh);

// metadata & paths
int         orus_fs_stat(const char* path, /*out*/ struct orus_fs_stat* out);
int         orus_fs_exists(const char* path); // 0/1
int         orus_fs_isdir(const char* path);  // 0/1
int         orus_fs_mkdir(const char* path, int perm_octal, int recursive);
int         orus_fs_remove(const char* path); // file or empty dir
int         orus_fs_rename(const char* oldp, const char* newp);

// directory listing (simple iterator)
typedef void* orus_dir_t;
orus_dir_t  orus_fs_opendir(const char* path);
const char* orus_fs_readdir(orus_dir_t dh); // returns entry name or NULL
int         orus_fs_closedir(orus_dir_t dh);
```

VM registers these as internal symbols:

```
__c_fs_open, __c_fs_close, __c_fs_read, __c_fs_write, __c_fs_flush,
__c_fs_seek, __c_fs_tell, __c_fs_stat, __c_fs_exists, __c_fs_isdir,
__c_fs_mkdir, __c_fs_remove, __c_fs_rename, __c_fs_opendir,
__c_fs_readdir, __c_fs_closedir
```

---

## Orus wrapper (std/fs.orus)

```orus
// std/fs.orus

// ===== Core bindings (hidden; not exported) =====
@[core("__c_fs_open")]    fn __c_fs_open(path: string, mode: i32, perm: i32) -> any
@[core("__c_fs_close")]   fn __c_fs_close(fh: any) -> i32
@[core("__c_fs_read")]    fn __c_fs_read(fh: any, buf: bytes, n: i64) -> i64
@[core("__c_fs_write")]   fn __c_fs_write(fh: any, buf: bytes, n: i64) -> i64
@[core("__c_fs_flush")]   fn __c_fs_flush(fh: any) -> i32
@[core("__c_fs_seek")]    fn __c_fs_seek(fh: any, off: i64, whence: i32) -> i64
@[core("__c_fs_tell")]    fn __c_fs_tell(fh: any) -> i64

@[core("__c_fs_stat")]    fn __c_fs_stat(path: string) -> Stat  // returns struct or raises
@[core("__c_fs_exists")]  fn __c_fs_exists(path: string) -> bool
@[core("__c_fs_isdir")]   fn __c_fs_isdir(path: string) -> bool
@[core("__c_fs_mkdir")]   fn __c_fs_mkdir(path: string, perm: i32, recursive: bool) -> i32
@[core("__c_fs_remove")]  fn __c_fs_remove(path: string) -> i32
@[core("__c_fs_rename")]  fn __c_fs_rename(oldp: string, newp: string) -> i32

@[core("__c_fs_opendir")]  fn __c_fs_opendir(path: string) -> any
@[core("__c_fs_readdir")]  fn __c_fs_readdir(dh: any) -> string?   // null when done
@[core("__c_fs_closedir")] fn __c_fs_closedir(dh: any) -> i32

// ===== Public API =====

// bitflags for open modes
const READ:   i32 = 1
const WRITE:  i32 = 2
const APPEND: i32 = 4
const CREATE: i32 = 8
const TRUNC:  i32 = 16

// seek whence
const SET: i32 = 0
const CUR: i32 = 1
const END: i32 = 2

// File handle (opaque to user code)
struct File:
    _h: any

fn open(path: string, mode: i32, perm: i32 = 0o644) -> File:
    let h = __c_fs_open(path, mode, perm)
    if h == null: raise_io("open failed: " + path)
    return File{ _h = h }

fn close(f: File):
    let rc = __c_fs_close(f._h)
    if rc != 0: raise_io("close failed")

fn read(f: File, n: i64) -> bytes:
    mut buf = make_bytes(n)         // std helper to allocate bytes
    let got = __c_fs_read(f._h, buf, n)
    if got < 0: raise_io("read failed")
    return slice_bytes(buf, 0, got) // trim to actual length

fn read_all(f: File) -> bytes:
    // naive: seek to end, get size, seek back, read
    let pos = __c_fs_tell(f._h)
    let end = __c_fs_seek(f._h, 0, END)
    _ = __c_fs_seek(f._h, pos, SET)
    return read(f, end - pos)

fn write(f: File, data: bytes):
    let n = __c_fs_write(f._h, data, len(data) as i64)
    if n < 0 or n != len(data) as i64: raise_io("write failed")

fn flush(f: File):
    if __c_fs_flush(f._h) != 0: raise_io("flush failed")

fn seek(f: File, off: i64, whence: i32 = SET) -> i64:
    let p = __c_fs_seek(f._h, off, whence)
    if p < 0: raise_io("seek failed"); return -1
    return p

// text helpers (UTF-8)
fn read_text(f: File) -> string:
    return decode_utf8(read_all(f))

fn write_text(f: File, s: string):
    write(f, encode_utf8(s))

// metadata
struct Stat:
    size: i64
    is_dir: bool
    is_file: bool
    mtime_ms: i64
    atime_ms: i64
    ctime_ms: i64
    mode: i32

fn exists(path: string) -> bool: return __c_fs_exists(path)
fn is_dir(path: string) -> bool: return __c_fs_isdir(path)
fn stat(path: string) -> Stat:   return __c_fs_stat(path)

fn mkdir(path: string, perm: i32 = 0o755, recursive: bool = false):
    if __c_fs_mkdir(path, perm, recursive) != 0:
        raise_io("mkdir failed: " + path)

fn remove(path: string):
    if __c_fs_remove(path) != 0:
        raise_io("remove failed: " + path)

fn rename(oldp: string, newp: string):
    if __c_fs_rename(oldp, newp) != 0:
        raise_io("rename failed: " + oldp + " -> " + newp)

// directory listing (simple, eager)
fn listdir(path: string) -> [string]:
    let dh = __c_fs_opendir(path)
    if dh == null: raise_io("opendir failed: " + path)
    mut out: [string] = []
    loop:
        let name = __c_fs_readdir(dh)
        if name == null: break
        push(out, name)
    _ = __c_fs_closedir(dh)
    return out

// ---- errors
fn raise_io(msg: string):
    // Use your existing try/catch model
    // Could raise a typed exception later; keep simple for v1
    throw "IOError: " + msg
```

> Note: `make_bytes` / `slice_bytes` / `encode_utf8` / `decode_utf8` are tiny helpers you can host in `std/bytes` and `std/encoding` (or inline minimal versions here to keep v1 small).

---

## Usage

```orus
use fs

// write a file (create or truncate)
let f = fs.open("hello.txt", fs.WRITE | fs.CREATE | fs.TRUNC)
fs.write_text(f, "Hello Orus!\n")
fs.flush(f)
fs.close(f)

// read it back
let g = fs.open("hello.txt", fs.READ)
let txt = fs.read_text(g)
print(txt)
fs.close(g)

// list directory
for name in fs.listdir("."):
    print("entry:", name)

// metadata
let st = fs.stat("hello.txt")
print("size:", st.size)
```

---

## Why this is minimal yet powerful

* **Tiny API**: one `File` type + ~15 functions covers 90% of use cases.
* **Binary + text** I/O with clear encoding boundary (bytes vs string).
* **Bitflag modes** avoid extra enums/keywords.
* **No new keyword**: only `@[core]` attributes for the bridge.
* **Encapsulation**: users can’t touch `__c_fs_*`; everything goes through `use fs`.

---

## Security & portability notes (v1 guardrails)

* **Path normalization** in C core to avoid `..` traversal if you later add a sandbox root.
* **Atomic write helper** (v2): write to `file.tmp` then `rename` → safe against partial writes.
* **Permissions**: respect `perm` on POSIX; ignore gracefully on Windows.
* **Text default**: UTF-8 only; no per-platform encodings (simplicity first).
* **No implicit file close**: make it explicit in v1; add RAII-style helper later if you add destructors.

---

## Tests you should add (short list)

* Create/Write/Read/Close roundtrip (text + bytes).
* Append mode correctness.
* Seek/tell behavior vs known content.
* listdir deterministic presence of written file.
* stat fields sanity (size increments).
* mkdir recursive + remove.
* Negative tests: open non-existing for READ; write to READ handle; remove nonexistent; rename to busy path → must raise IO error.