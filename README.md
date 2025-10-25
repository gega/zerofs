# zerofs

**zerofs** is a dual-flash based embedded filesystem for devices where reads and writes happen in separate phases.
It’s made for systems that *don’t behave like general-purpose computers* — for example, a sensor that occasionally logs data but spends most of its time reading back those files.

zerofs optimized exactly for this specific usage pattern. No directories, no journaling, no heap allocations, no power-failure recovery. Just deterministic, low-memory file access with full flash utilization. Data flash sectors contains no administrative data, just the payload. The superblock contains every metadata which besides in a separate flash area.

*NOTE: Zerofs should be used only for battery powered device when the power failure is predictable.*

---

## Why

Most embedded filesystems provide the "usual" general filesystem API: they sacrifice RAM, flash, and CPU time to support every scenario.
zerofs is a specialist. It focuses on **devices with distinct read/write phases**, delivering:

* Minimal RAM usage
  - less than 200 bytes in READ mode
  - ~1K in WRITE mode
* Predictable behavior
* Simplified flash management
* Close to 100% flash utilization

Ideal for battery-backed systems and devices where you know the exact access pattern.

---

## What

| file       | purpose                                |
|------------|----------------------------------------| 
| zerofs.h   | the filesystem itself                  |
| flash.h    | flash simulation header                |
| flash.c    | flash simulation implementation        |
| test.h     | test related constants and definitions |
| zerofs.c   | lua test runner for zerofs backend     |
| littlefs.c | lua test runner for LittleFS backend   |
| test1.lua  | lua test script                        |

---

## Features

* ~1 KB RAM usage total
* No recursion, no heap
* Flat file structure (no directories)
* Distinct **READ** and **WRITE** modes
* Write buffer (~1 KB) only needed during WRITE mode
* Nearly 100% flash utilization
* Dual flash design (e.g. MCU flash for metadata + external flash for data)
* Header-only implementation
* Background erase support
* Generic flash interface layer
* Designed for battery-backed environments

---

## Integration

zerofs is header-only. Just define the implementation macro before including it:

```c
#define ZEROFS_IMPLEMENTATION
#include "zerofs.h"
```

Include this once in your project (e.g. in a `.c` file).
Everywhere else, just include `"zerofs.h"` normally.

---

## Design Overview

zerofs separates read and write operations into exclusive modes.

* **WRITE mode:** requires a temporary buffer (~1 KB) supplied by the user.
* **READ mode:** buffer is not needed and can be reused for something else (like cache or DMA).

This mode separation allows deterministic memory usage and faster access with minimal overhead.

---

## Demo

### zerofs

[![asciicast](https://asciinema.org/a/XXMliaC3RemByzUO1Z5Q1EsBN.svg)](https://asciinema.org/a/XXMliaC3RemByzUO1Z5Q1EsBN)

### LittleFS (for comparison)

[![asciicast](https://asciinema.org/a/M15Q0NelQnfHv3SEn3EnuyonZ.svg)](https://asciinema.org/a/M15Q0NelQnfHv3SEn3EnuyonZ)

Under the same workload, zerofs performs **about half the flash operations** of LittleFS.
LittleFS provides more features and flexibility, but for certain patterns, zerofs offers far better **RAM and speed efficiency**.

| fs       | time       |
|----------|------------|
| littlefs | 86772.4 ms |
| zerofs   | 44204.2 ms |

*Note1: measurements based on the timings of nRF52 series and the BY25Q32ES SPI flash IC.*

*Note2: LittleFS is configured to use similar amount of RAM what zerofs needs.*

---

## Test Harness

A flash simulation layer with integrated **Lua scripting** and **TUI visualization**.

### Highlights

* Flash access simulator with time measurement
* Lua scripting for file operations
* Compatible backend for both zerofs and LittleFS
* Visual block map with file colors and inverse highlight for recent writes
* Adjustable speed (`<`, `>`)
* Step mode (`s`) and scrollable console
* Script-controlled warnings and test failures
* Optional log saving

Perfect for debugging, testing workloads, or benchmarking behavior.

---

## Static Configuration Example

```c
// Total flash size in KB
#define ZEROFS_FLASH_SIZE_KB (4096)

// Sector size of data flash (minimum erase unit)
#define ZEROFS_FLASH_SECTOR_SIZE (4096)

// Maximum number of files (checked by static asserts)
#define ZEROFS_MAX_NUMBER_OF_FILES (191)

// Sector size of superblock flash (usually MCU internal flash)
#define ZEROFS_SUPER_SECTOR_SIZE (4096)

// Supported extensions (sorted, <255 total)
#define ZEROFS_EXTENSION_LIST \
    X("bin")                 \
    X("txt")                 \
    X("zip")
```

---

## API Reference

All functions return `0` on success or a negative error code on failure.

### Error Codes

```c
#define ZEROFS_ERR_MAXFILES   (-2)  // Reached ZEROFS_MAX_NUMBER_OF_FILES
#define ZEROFS_ERR_NOTFOUND   (-3)  // File not found
#define ZEROFS_ERR_READMODE   (-4)  // Operation not allowed in READ mode
#define ZEROFS_ERR_NOSPACE    (-5)  // No space left on device
#define ZEROFS_ERR_OPEN       (-6)  // Failure during open
#define ZEROFS_ERR_ARG        (-7)  // Invalid argument
#define ZEROFS_ERR_WRITEMODE  (-8)  // Operation not allowed in WRITE mode
#define ZEROFS_ERR_OVERFLOW   (-9)  // Seek/write overflow
#define ZEROFS_ERR_BADSECTOR (-10)  // Bad sector detected
```

---

### Initialization

```c
int zerofs_init(struct zerofs *zfs, uint8_t verify, const struct zerofs_flash_access *fls_acc);
```

**Parameters:**

| Name      | Description                                                       |
| --------- | ----------------------------------------------------------------- |
| `zfs`     | Filesystem instance structure to be initialized                   |
| `verify`  | Write verification rate. `0` = none, `N` = verify every Nth write |
| `fls_acc` | Flash access callbacks and configuration                          |

#### `struct zerofs_flash_access`

```c
struct zerofs_flash_access {
    int (*fls_write)(void *ud, uint32_t addr, const uint8_t *data, uint32_t len);
    int (*fls_read)(void *ud, uint32_t addr, uint8_t *data, uint32_t len);
    int (*fls_erase)(void *ud, uint32_t addr, uint32_t len);
    uint8_t *superblock_banks;
    void *data_ud;
    void *super_ud;
};
```

| Field              | Description                                                                                                                                                                                                                                                                                                                             |
| ------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `fls_write`        | Function pointer to write bytes to flash                                                                                                                                                                                                                                                                                                |
| `fls_read`         | Function pointer to read bytes from flash                                                                                                                                                                                                                                                                                               |
| `fls_erase`        | Function pointer to erase sectors                                                                                                                                                                                                                                                                                                       |
| `superblock_banks` | Pointer to a **memory-mapped flash region** used for the superblock (metadata). Reads are done directly from memory. Writes still go through `fls_write`. <br> If no memory-mapped flash is available, this must point to a RAM buffer large enough to hold the superblock (~8 KB). This is less efficient in RAM usage, but supported. |
| `data_ud`          | User data pointer passed to data flash callbacks                                                                                                                                                                                                                                                                                        |
| `super_ud`         | User data pointer passed to superblock flash callbacks                                                                                                                                                                                                                                                                                  |

---

### Mode Management

```c
int zerofs_is_readonly_mode(struct zerofs *zfs);
```

Returns nonzero if the filesystem is currently in **READ mode**.

```c
int zerofs_readonly_mode(struct zerofs *zfs, uint8_t *sector_map);
```

Switches between **READ** and **WRITE** mode.
If `sector_map` is `NULL`, enters READ mode.
If non-`NULL`, enters WRITE mode using `sector_map` as a temporary buffer.
The buffer size must be `(ZEROFS_FLASH_SIZE_KB * 1024) / ZEROFS_FLASH_SECTOR_SIZE`.

---

### File Operations

```c
int zerofs_format(struct zerofs *zfs);
```

Erases all files and resets the filesystem.

^⎚-⎚^
```c
int zerofs_open(struct zerofs *zfs, struct zerofs_file *fp, const char *name);
```

Opens a file for reading (READ mode only).

✒
```c
int zerofs_create(struct zerofs *zfs, struct zerofs_file *fp, const char *name);
```

Creates and opens a file for writing (WRITE mode only).

✒
```c
int zerofs_append(struct zerofs *zfs, struct zerofs_file *fp, const char *name);
```

Opens an existing file for appending (WRITE mode only).

✒
```c
int zerofs_close(struct zerofs_file *fp);
```

Closes a file.
For read-only files, this is effectively a no-op.

^⎚-⎚^
```c
int zerofs_read(struct zerofs_file *fp, uint8_t *buf, uint32_t len);
```

Reads up to `len` bytes from a file opened for reading.

✒
```c
int zerofs_write(struct zerofs_file *fp, uint8_t *buf, uint32_t len);
```

Writes up to `len` bytes to a file opened for writing.

^⎚-⎚^
```c
int zerofs_seek(struct zerofs_file *fp, int32_t pos);
```

Moves the read pointer within a file.
Negative offsets are relative to the end of the file.
Seeking is **not supported during write mode**.

✒
```c
int zerofs_delete(struct zerofs *zfs, const char *name);
```

Deletes a file by name.

---

### Background Maintenance

```c
int zerofs_background_erase(struct zerofs *zfs);
```

Performs background flash erases while in **READ mode**.
Does not block reads, but must complete before switching to WRITE mode.

# Third-party components

LittleFS v2.11.2 and Lua v5.4.8 are included here to make sure build would succeed.
