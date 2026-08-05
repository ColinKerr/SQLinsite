Answer: Yes — a SQLite page maps to a Cloud Backed SQLite block using only manifest.bcv + the SQLite file

Cloud Backed SQLite stores each database as a sequence of fixed-size blocks, and the mapping is a pure, contiguous, offset-based one — no cloud round-trip needed to compute it.

The mapping

For 1-based page P:
byteOffset  = (P - 1) * pageSize
blockIndex  = byteOffset / blockSize          // integer division
blockId     = dbBlockArray[blockIndex]        // nNamesize-byte entry
blockFile   = hex(blockId) + ".bcv"           // the object name in the container
inBlockOff  = byteOffset % blockSize          // where the page sits inside the block
Because blockSize is a power of two and always ≥ pageSize (default 4 MiB block vs ≤ 64 KiB page), every page lies wholly inside one block; pagesPerBlock = blockSize / pageSize (1024 for 4 MiB/4 KiB).

Where each input comes from

- The SQLite file supplies pageSize (page-1 header, big-endian bytes 16–17; a stored 1 means 65536). That's the only thing needed to turn a page number into a byte offset. (Page count is useful as a sanity check.)
- manifest.bcv supplies everything else — it's a self-contained binary index.

manifest.bcv layout (confirmed from the v4 parser, big-endian u32s)

Header:

┌────────┬───────────────────────────────────────────┐
│ offset │                   field                   │
├────────┼───────────────────────────────────────────┤
│ 0      │ version (currently 4)                     │
├────────┼───────────────────────────────────────────┤
│ 4      │ block size in bytes (szBlk)               │
├────────┼───────────────────────────────────────────┤
│ 8      │ number of databases (nDb)                 │
├────────┼───────────────────────────────────────────┤
│ 12     │ number of delete/GC entries               │
├────────┼───────────────────────────────────────────┤
│ 16     │ block-id size in bytes (nNamesize, 12–32) │
├────────┼───────────────────────────────────────────┤
│ 20     │ max db id                                 │
└────────┴───────────────────────────────────────────┘

Then one per-database header (≈152 bytes: a few u32s + a 128-byte name), which gives that DB's name, the offset of its block array, and the block count. The block array is just nBlk consecutive nNamesize-byte block IDs, ordered by position in the file — array index i = database bytes [i*szBlk, (i+1)*szBlk).

I verified the contiguity claim from the VFS read path itself, so it's not an inference:
i64 iBlk = iOfst / szBlk;                     // block index = offset / blocksize
u8 *pBlk = &pManDb->aBlkLocal[nNameBytes*iBlk];  // block id = i-th array entry
...  nNew = (size + szBlk - 1) / szBlk;        // nBlocks = ceil(size/blocksize)

Caveats to get it right

1. Pick the correct database. A manifest can describe several databases; you select the per-DB entry by its name (zDName). The SQLite file doesn't carry its CBS name, so the caller must know which named DB it is (trivial if the container holds one).
2. Committed state only. The on-disk manifest.bcv holds the committed block list (aBlkOrig), which matches a committed SQLite file. A daemonless client with uncommitted local edits uses an in-memory aBlkLocal that isn't in the plain manifest — out of scope for a static snapshot.
3. Check the version. The layout above is manifest version 4. The public header constants (BCV_MANIFEST_HEADER_BYTES 20, ..._DBHEADER_BYTES 156) don't match the v4 parser's 24 / 152 — so read the exact offsets from the version field and validate against a real manifest before hard-coding them.
4. Block naming / addressing. The container object name is the hex of the block ID + .bcv. For IDs ≥ 24 bytes the first 16 bytes are an MD5 of block contents (content-addressed dedup) — irrelevant to the mapping, only to naming.
5. Last block is partial — fine; the page still resolves to exactly one block. Deleted-block entries at the manifest tail are for GC and aren't part of any DB's array.
6. Encryption (if used) changes block contents, not the page→block mapping; you'd need the key to read bytes but not to compute the block.

Net

Everything needed to answer "which cloud block holds page P" is derivable offline: pageSize from the SQLite file, and blockSize + the ordered per-DB block-ID array from manifest.bcv. The only external knowledge required is which named database in the manifest the file corresponds to. So for the visualizer this is very feasible — e.g., a per-page "block" column (block index + .bcv id) computed with nothing but those two files.

Want me to prototype a small reader that parses manifest.bcv and, given the SQLite file, emits page→block mappings (or annotates the map with a blockId per page)?