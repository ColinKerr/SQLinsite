# Lessons Learned

Catalogs issues and problems that caused a change in architecture or implementation that are helpful for future design and implementation decisions.

## `SQLinsite visualize serve`

### Performance

Original implementation stored the whole map as JSON and drew one DOM node per page with D3. That was too slow. So the updated design stores the map in a SQLite file, the server answers **page-range** queries, and the browser draws blocks directly on the canvas, dropping D3.