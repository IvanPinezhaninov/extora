# Third-party notices

The installed Extora library uses the third-party software listed below. The
referenced runtime license files are installed in the `LICENSES` directory.

### xxHash

- Default bundled version: 0.8.3
- Project: <https://github.com/Cyan4973/xxHash>
- License: BSD 2-Clause
- License text: [LICENSES/xxhash-BSD-2-Clause.txt](LICENSES/xxhash-BSD-2-Clause.txt)

Extora compiles the BSD-licensed `xxhash.c` and `xxhash.h` library files into
its static and shared libraries. Extora does not use or distribute the
GPL-licensed `xxhsum` command-line utility.

### SQLite

- Default bundled version: 3.53.2
- Project: <https://www.sqlite.org/>
- Copyright status: public domain

Extora compiles the official SQLite amalgamation into the library unless
`EXTORA_USE_SYSTEM_SQLITE` is enabled. The SQLite authors have dedicated the
deliverable SQLite code and documentation to the public domain. See the
[official SQLite copyright statement](https://www.sqlite.org/copyright.html).
