# Formatting Rules

Shared across the entire application unless explicitly specified in a location.

- All shared text formatting code goes into: web/src/core/format.ts.
- Each shared formatting control goes into it's own file in the web/src/components directory.
- Tests go in appropriately named files next to the code/component files.

## Count of bytes

Size in bytes is formatted in kilobytes (KB), megabytes (MB) or gigabytes (GB).

### Rules

- Convert to the largest unit that gives at least one whole number digit.
- Never show more than two digits to the right of the decimal point
- Format like `[converted value] [unit]` e.g. `42.42 GB`.

## Count of page sector bytes

Custom formatting specifically for size of sectors within a page.  e.g. number of bytes used by a cell, unallocated spec, size of a blob column in a row.  Formatted in bytes (B).

> NOTE: SQLite limits page size to 64k or 65536 bytes, so count of bytes for a sector in a page will never be larger than that.

### Rules

- Always show in unconverted bytes
- Format like `[converted value][unit]` e.g. `42B`

## Generic Counts

For all counts that do not have an explicit formatting rule.  e.g. count of rows or count of pages.

### Rules

- Use thousands separator. e.g. 4,599,739
- Never show decimal point

## Page id numbers

For pointers to pages, like rightmostPointer, nextPage, owned by, etc.  Does not include page id numbers in the b-tree tree.

### Rules

- Represent using the PageCard component
- Have it link behavior defined for that view.  If none is defined choose selecting the page in the b-tree tree