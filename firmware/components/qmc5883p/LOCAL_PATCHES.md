# Local patches to duruofu/qmc5883p

| Item | Value |
| --- | --- |
| Upstream | `duruofu/qmc5883p` 0.1.0 from the ESP Component Registry (https://github.com/DuRuofu/esp-idf-components/tree/master/qmc5883p) |
| Vendored from | `~/projects/qmc5883p-espidf-example/components/duruofu__qmc5883p`, the copy validated alone on the XIAO ESP32-S3 |
| Reference | QST QMC5883P datasheet, Rev A, document 13-52-19 (https://cdn-shop.adafruit.com/product-files/6388/C2847467.pdf, the link in TD §13) |

`CHECKSUMS.json` and `.component_hash` describe upstream 0.1.0, not the patched files. This is a local component under `firmware/components/`, so the component manager does not verify them.

`qmc5883p.c` and `include/qmc5883p.h` use CRLF line endings upstream; keep them, so the diff against upstream stays limited to the patches below.

## Patches (Set 3, R3.2, 2026-10-10)

1. **SCL speed is an argument.** `qmc5883p_init(dev, bus, addr, scl_speed_hz)` replaces the upstream three-argument form, which hard-coded 100 kHz.
   - Passing 0 selects `QMC5883P_SCL_SPEED_DEFAULT_HZ` (100 kHz, the upstream behaviour).
   - DirecSun passes 400000. The chip supports 100 and 400 kHz (datasheet §5.4), and TD §5.3 runs the bus at 400 kHz.
   - The component's own `test_app/main/main.c` now passes its 100 kHz constant.
2. **Field-range enum matched to the chip.** Upstream mapped RNG<1:0> as 00 = 2 G, 01 = 8 G, 10 = 12 G, 11 = 30 G. Datasheet Table 18 (Control Register 2, page 17/18) says 00 = 30 G, 01 = 12 G, 10 = 8 G, 11 = 2 G.
   - Upstream's `qmc5883p_set_range(QMC5883P_RNG_8G)` therefore wrote 01 and put the chip at ±12 G.
   - With the fix, `qmc5883p_init` writes CTRL2 = 0x08. That is exactly the datasheet's setup example (§7.1: 0x29 = 0x06, 0x0B = 0x08, 0x0A = 0xCD).
3. **Sensitivity constants from the datasheet.** Table 2 "Specifications", Sensitivity row (page 5/18): 1000, 2500, 3750, and 15000 LSB/G for ±30, ±12, ±8, and ±2 G.
   - Upstream used 12000 for both 8 G and 2 G (its comment said "Docs say 3000 for 8G in some places, others 4096").
   - Combined with patch 2, upstream reported the field at about 2500 / 12000 = 0.21 times its true value, since the chip was really at ±12 G and was scaled with 12000.
   - Heading uses only the ratio of the horizontal axes, so control was never affected, but the gauss values in telemetry were wrong.
4. **Mode enum matched to the chip.** Datasheet Table 17 (Control Register 1, page 16/18): MODE<1:0> 00 = suspend, 01 = normal, 10 = single, 11 = continuous.
   - Upstream named 01 "continuous", with "normal" as an alias. Now each enum value names its real mode.
   - `qmc5883p_init` is unchanged and still writes CTRL1 = 0xCD (OSR2 = 11, OSR1 = 00, ODR = 200 Hz, MODE = 01 normal). That is the datasheet's normal-mode example.
   - Normal mode measures at the ODR, which is the "200 Hz output rate" FD §10.2 asks for. True continuous mode ignores ODR and runs at up to 1500 Hz.

## Not patched, noted

- `qmc5883p_read_register`, `qmc5883p_write_register`, and the functions built on them use a fixed 1000 ms transaction timeout. DirecSun's `direcsun_drivers/mag` calls `i2c_master_transmit_receive` on `dev->dev_handle` directly, with its own short timeout, for the per-tick reads, and uses the library functions only at boot.
- `qmc5883p_init` keeps going after a failed chip-ID read and returns the first write error. `direcsun_drivers/mag` checks the chip ID itself and removes the device from the bus on failure.
