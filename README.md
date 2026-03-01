# FlippeRFID (Flipper Zero)

External Flipper Zero app with:

- visual UI (menu, scan, write, clone, config)
- periodic UHF inventory
- tag persistence on SD card
- modular architecture (`driver` layer + module implementations)

## Project Structure

- `application.fam`: app metadata
- `flipper_rfid_app.c`: main UI and app flow (uses modular driver interface)
- `rfid_driver.*`: common RFID driver interface (scalable)
- `RfidModuleFm504/Fm505/Fm507/Fm509/Fm505A`: Fonkan family profiles
- `RfidModuleRe40` (Zebra): integrated base module (commands not implemented yet)
- `uhf_uart.*`: UART transport for UHF readers
- `uhf_protocol.*`: EPC Gen2 command building/parsing (FM504-family protocol)
- `uhf_reader.*`: UHF read/write logic
- `storage_tags.*`: persistence in `/ext/apps_data/flipperrfid/` (with backward read fallback to `/ext/apps_data/fm504_rfid/`)

## Scalable Architecture

- UI does not call `uhf_*` directly.
- UI uses `rfid_driver_*` as an abstraction layer.
- Module can be selected in UI (`Fonkan FM504/FM505/FM507/FM509/FM505A` or `Zebra RE40`).
- To add a new reader, implement it in `rfid_driver.c` without changing UI screens.

## FM504 Pin Mapping -> Flipper

1. `Reader GND` -> `Flipper GND`
2. `Reader TX` -> `Flipper RX`
3. `Reader RX` -> `Flipper TX`
4. `Reader EN` -> `Flipper PA7` (`gpio_ext_pa7`, GPIO header pin 2, MOSI line)
5. `Reader VCC` -> external power source (recommended for UHF)

### Software Power Control (`EN`)

- `Start Scan`: sets `EN` HIGH, waits for stabilization, then starts scanning.
- `Stop Scan`: sets `EN` LOW.
- On app exit or module change: `EN` is forced LOW.
- For write operations: `EN` is enabled temporarily, then disabled again.

## Current Status

Based on the validated Python project (`FM-503-UHF-RFID-Reader-main`), the app follows the ASCII command model:

- General format: `<LF><CMD><args><CR>`
- EPC read (single): `<LF>R1,0,<words><CR>`
- TID read: `<LF>R2,<addr>,<words><CR>`
- Multi EPC inventory: `<LF>U<max><CR>` (reference, not implemented yet)
- Write/lock ACK: response contains `<OK>`

Current app capabilities:

- `FM503`/`FM504`/`FM505` usually share the same command family (validate against your firmware).
- `uhf_uart.c` implements real UART with RX buffering.
- Main menu:
  - `Scan`
  - `Write Tag`
  - `Write USER`
  - `Saved Tags`
  - `Clone`
  - `Check Protection`
  - `Config`
  - `About`
- `Config` submenu:
  - `Module`
  - `Read Mode` (`EPC`, `TID`, `USER`, `ALL`)
  - `TX Power` (`-2dB` to `27dB`)
  - `Read Rate (ms)`
  - `Access Password`
- Available module profiles:
  - `Fonkan FM504` (reference range: ~0-40cm at 1dB)
  - `Fonkan FM505` (reference range: ~0-2m at 3dB)
  - `Fonkan FM507` (reference range: ~0-3m at 4dB)
  - `Fonkan FM509` (reference range: ~0-3.5m at 5dB)
  - `Fonkan FM505A` (reference range: ~0-4m at 5.5dB)
  - `Zebra RE40`
- Compact scan screen:
  - header: `Scan > <MODE> <PWR>dB <RATE>ms`
  - large read area
  - buttons: `Save`, `Start/Stop`, `Clear`
- EPC parsing from `R` response (`CRC16 + PC + EPC` assumption)
- TID parsing from `R` response as hex block
- USER read/write supported
- Write command format may vary by firmware (`W1,2,<len_words>,<epc_hex>` currently used)

## Persistence and Robustness

- Current data path: `/ext/apps_data/flipperrfid/`
- Backward compatibility: if new files are missing, data is loaded from `/ext/apps_data/fm504_rfid/`
- `Saved Tags` hardened to prevent freezes from corrupted CSV:
  - line-by-line loading
  - line length limit
  - scanned-byte limit
  - field sanitization for invalid characters

## Pending Hardware Validation

Validate these against your FM504 traces/manual:

1. `uhf_uart_open`, `uhf_uart_send`, `uhf_uart_read` with real wiring
2. `uhf_protocol_make_write_epc_cmd` against actual FM504 write traces
3. If using `U` multi-tag mode, implement line-based multi-tag parser

## App Flow

1. `Start scan`: starts periodic inventory
2. `View captures`: shows EPC/TID/USER based on selected mode
3. `Write EPC`: edit HEX and write to target tag
4. `Save captures`: saves to `saved_tags.csv`
5. `Clone`: capture source tag and write to destination tag
6. `Write USER`: edit and write USER bank
7. `Check Protection`: controlled write check to detect lock behavior

## Build (ufbt)

```bash
cd /Users/fernando/Source_Code/FlipperZero_RFID/flipper_rfid_app
ufbt build
ufbt launch
```

## Recommended Next Step

Capture and share one real raw response frame from your FM504 for each operation (EPC read, TID read, write ACK).
With that, protocol parsing and write reliability can be finalized safely.
