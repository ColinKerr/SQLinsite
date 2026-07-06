#include "profile/vfs_shim.hpp"

#include <chrono>
#include <cstring>

#include <sqlite3.h>

#include "profile/access_sink.hpp"
#include "profile/page_index.hpp"
#include "profile/profiling_context.hpp"

const char* const kSQLINSITEVfsName = "sqlinsite";

namespace {

sqlite3_vfs* gRootVfs = nullptr;

// A wrapper sqlite3_file. `base` must be first so a ShimFile* is a valid
// sqlite3_file*. The real file handle lives in memory immediately after this
// struct (the registered szOsFile reserves room for it).
struct ShimFile {
    sqlite3_file base;
    sqlite3_file* real;
    bool logged;
};

sqlite3_file* realOf(sqlite3_file* file) {
    return reinterpret_cast<ShimFile*>(file)->real;
}

std::int64_t nowTicks() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

void logAccess(const ShimFile* shim, sqlite3_int64 offset, AccessType access,
               std::int64_t start, std::int64_t end) {
    if (!shim->logged) {
        return;
    }
    ProfilingContext& ctx = profilingContext();
    if (ctx.out == nullptr) {
        return;
    }
    const std::int64_t pageNumber = pageNumberFor(offset, ctx.pageSize);
    const std::int64_t base = ctx.relativeTiming ? ctx.timeBaseline : 0;
    ctx.out->record(ctx.sessionName, ctx.statementIndex, start - base,
                    end - base, pageNumber, access);
}

// --- IO methods: read/write are instrumented, the rest delegate. ---

int shimClose(sqlite3_file* file) {
    sqlite3_file* real = realOf(file);
    int rc = real->pMethods ? real->pMethods->xClose(real) : SQLITE_OK;
    file->pMethods = nullptr;
    return rc;
}

int shimRead(sqlite3_file* file, void* buf, int amount, sqlite3_int64 offset) {
    auto* shim = reinterpret_cast<ShimFile*>(file);
    const std::int64_t start = nowTicks();
    int rc = shim->real->pMethods->xRead(shim->real, buf, amount, offset);
    const std::int64_t end = nowTicks();
    logAccess(shim, offset, AccessType::Read, start, end);
    return rc;
}

int shimWrite(sqlite3_file* file, const void* buf, int amount,
              sqlite3_int64 offset) {
    auto* shim = reinterpret_cast<ShimFile*>(file);
    const std::int64_t start = nowTicks();
    int rc = shim->real->pMethods->xWrite(shim->real, buf, amount, offset);
    const std::int64_t end = nowTicks();
    logAccess(shim, offset, AccessType::Write, start, end);
    return rc;
}

int shimTruncate(sqlite3_file* file, sqlite3_int64 size) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xTruncate(r, size);
}

int shimSync(sqlite3_file* file, int flags) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xSync(r, flags);
}

int shimFileSize(sqlite3_file* file, sqlite3_int64* size) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xFileSize(r, size);
}

int shimLock(sqlite3_file* file, int level) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xLock(r, level);
}

int shimUnlock(sqlite3_file* file, int level) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xUnlock(r, level);
}

int shimCheckReservedLock(sqlite3_file* file, int* result) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xCheckReservedLock(r, result);
}

int shimFileControl(sqlite3_file* file, int op, void* arg) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xFileControl(r, op, arg);
}

int shimSectorSize(sqlite3_file* file) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xSectorSize(r);
}

int shimDeviceCharacteristics(sqlite3_file* file) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xDeviceCharacteristics(r);
}

int shimShmMap(sqlite3_file* file, int pg, int sz, int extend, void volatile** p) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xShmMap(r, pg, sz, extend, p);
}

int shimShmLock(sqlite3_file* file, int offset, int n, int flags) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xShmLock(r, offset, n, flags);
}

void shimShmBarrier(sqlite3_file* file) {
    sqlite3_file* r = realOf(file);
    r->pMethods->xShmBarrier(r);
}

int shimShmUnmap(sqlite3_file* file, int deleteFlag) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xShmUnmap(r, deleteFlag);
}

int shimFetch(sqlite3_file* file, sqlite3_int64 offset, int amount, void** pp) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xFetch(r, offset, amount, pp);
}

int shimUnfetch(sqlite3_file* file, sqlite3_int64 offset, void* p) {
    sqlite3_file* r = realOf(file);
    return r->pMethods->xUnfetch(r, offset, p);
}

const sqlite3_io_methods kShimIoMethods = {
    3,  // iVersion
    shimClose,
    shimRead,
    shimWrite,
    shimTruncate,
    shimSync,
    shimFileSize,
    shimLock,
    shimUnlock,
    shimCheckReservedLock,
    shimFileControl,
    shimSectorSize,
    shimDeviceCharacteristics,
    shimShmMap,
    shimShmLock,
    shimShmBarrier,
    shimShmUnmap,
    shimFetch,
    shimUnfetch,
};

// --- VFS methods: xOpen is instrumented, the rest delegate to the root VFS. ---

int shimOpen(sqlite3_vfs*, sqlite3_filename name, sqlite3_file* file, int flags,
             int* outFlags) {
    auto* shim = reinterpret_cast<ShimFile*>(file);
    shim->real = reinterpret_cast<sqlite3_file*>(
        reinterpret_cast<char*>(shim) + sizeof(ShimFile));
    shim->logged = (flags & SQLITE_OPEN_MAIN_DB) != 0;

    int rc = gRootVfs->xOpen(gRootVfs, name, shim->real, flags, outFlags);
    if (rc != SQLITE_OK) {
        shim->base.pMethods = nullptr;
        return rc;
    }
    shim->base.pMethods = &kShimIoMethods;
    return SQLITE_OK;
}

int shimDelete(sqlite3_vfs*, const char* name, int syncDir) {
    return gRootVfs->xDelete(gRootVfs, name, syncDir);
}

int shimAccess(sqlite3_vfs*, const char* name, int flags, int* result) {
    return gRootVfs->xAccess(gRootVfs, name, flags, result);
}

int shimFullPathname(sqlite3_vfs*, const char* name, int outLen, char* out) {
    return gRootVfs->xFullPathname(gRootVfs, name, outLen, out);
}

void* shimDlOpen(sqlite3_vfs*, const char* filename) {
    return gRootVfs->xDlOpen(gRootVfs, filename);
}

void shimDlError(sqlite3_vfs*, int byteCount, char* errMsg) {
    gRootVfs->xDlError(gRootVfs, byteCount, errMsg);
}

void (*shimDlSym(sqlite3_vfs*, void* handle, const char* symbol))(void) {
    return gRootVfs->xDlSym(gRootVfs, handle, symbol);
}

void shimDlClose(sqlite3_vfs*, void* handle) {
    gRootVfs->xDlClose(gRootVfs, handle);
}

int shimRandomness(sqlite3_vfs*, int byteCount, char* out) {
    return gRootVfs->xRandomness(gRootVfs, byteCount, out);
}

int shimSleep(sqlite3_vfs*, int microseconds) {
    return gRootVfs->xSleep(gRootVfs, microseconds);
}

int shimCurrentTime(sqlite3_vfs*, double* out) {
    return gRootVfs->xCurrentTime(gRootVfs, out);
}

int shimGetLastError(sqlite3_vfs*, int byteCount, char* out) {
    return gRootVfs->xGetLastError(gRootVfs, byteCount, out);
}

int shimCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64* out) {
    return gRootVfs->xCurrentTimeInt64(gRootVfs, out);
}

}  // namespace

int registerSQLINSITEVfs() {
    if (sqlite3_vfs_find(kSQLINSITEVfsName) != nullptr) {
        return SQLITE_OK;
    }
    gRootVfs = sqlite3_vfs_find(nullptr);
    if (gRootVfs == nullptr) {
        return SQLITE_ERROR;
    }

    static sqlite3_vfs vfs{};
    vfs.iVersion = 3;
    vfs.szOsFile = static_cast<int>(sizeof(ShimFile)) + gRootVfs->szOsFile;
    vfs.mxPathname = gRootVfs->mxPathname;
    vfs.zName = kSQLINSITEVfsName;
    vfs.xOpen = shimOpen;
    vfs.xDelete = shimDelete;
    vfs.xAccess = shimAccess;
    vfs.xFullPathname = shimFullPathname;
    vfs.xDlOpen = gRootVfs->xDlOpen ? shimDlOpen : nullptr;
    vfs.xDlError = gRootVfs->xDlError ? shimDlError : nullptr;
    vfs.xDlSym = gRootVfs->xDlSym ? shimDlSym : nullptr;
    vfs.xDlClose = gRootVfs->xDlClose ? shimDlClose : nullptr;
    vfs.xRandomness = shimRandomness;
    vfs.xSleep = shimSleep;
    vfs.xCurrentTime = shimCurrentTime;
    vfs.xGetLastError = shimGetLastError;
    vfs.xCurrentTimeInt64 = shimCurrentTimeInt64;

    return sqlite3_vfs_register(&vfs, 0);
}
