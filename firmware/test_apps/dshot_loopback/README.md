# DShot loopback test app (R3.1)

Proves what actually leaves the DShot pin, with three independent instruments: an on-chip RMT loopback, an oscilloscope, and the ESC's own arming tones.

## Safety

**The ESC signal pin D8 (GPIO7) receives only DShot 0 from this app.** The throttle test vectors (48, 1046, 2047) go out on D2 (GPIO3), which nothing but the loopback jumper touches. The ESC can stay wired to D8 while this app runs, and the motor cannot turn: the ESC sees nothing but zero throttle.

**Never connect the ESC signal wire to D2.**

## Wiring

| From | To | Why |
| --- | --- | --- |
| D2 (GPIO3) | D3 (GPIO4) | Jumper: loopback TX into the RMT RX channel |
| D8 (GPIO7) | ESC signal (optional) and oscilloscope probe | DShot 0 stream, one frame every 2 ms |
| GND | ESC signal ground and scope ground | Shared reference |

## Run

```bash
tools/idf.sh -C test_apps/dshot_loopback set-target esp32s3      # once
tools/idf.sh -C test_apps/dshot_loopback -p /dev/ttyACM0 build flash monitor
```

## Expected output

```
W dshot_loopback:  ESC pin D8 (GPIO7) receives ONLY DShot 0 from this app.
I dshot_loopback: D8: DShot150 zero stream started (one frame every 2 ms)
I dshot_loopback: PASS DShot150 value    0: sent 0x0000 got 0x0000 crc ok | ...
I dshot_loopback: PASS DShot150 value   48: sent 0x0606 got 0x0606 crc ok | '1' high 5000 ns ..., '0' high 2500 ns ...
I dshot_loopback: PASS DShot150 value 1046: sent 0x82C6 got 0x82C6 crc ok | ...
I dshot_loopback: PASS DShot150 value 2047: sent 0xFFEE got 0xFFEE crc ok | ...
I dshot_loopback: PASS DShot150 control: sent bad-CRC 0x0607, got 0x0607, decoder reported a CRC error
I dshot_loopback: PASS DShot300 value    0: ...           ('1' high 2500 ns, '0' high 1250 ns)
...
I dshot_loopback: LOOPBACK RESULT: PASS 10/10
I dshot_loopback: D8: streaming DShot150 zeros for the oscilloscope and ESC checks
I dshot_loopback: D8 status: 2500 frames sent, 0 dropped
```

The value-0 frame has no '1' bits, so its '1' high time prints 0 ns with n=0. Every line that says FAIL names what went wrong. "nothing captured on D3" means the jumper is missing.

## Oscilloscope on D8 (DShot150)

| Quantity | Expected |
| --- | --- |
| Bit period | 6.67 us, within 2% |
| High time of a '0' bit | 2.50 us (every bit of DShot 0 is a '0') |
| Frame length | 16 bits, about 107 us |
| Frame repetition | every 2.0 ms |

## ESC arming tones

With the ESC powered from the bench supply and its signal on D8, the ESC sees the zero stream and plays its arming tones, a low beep and then a high beep (TD section 6.3). No tones, or the no-signal pattern, means the frames are not reaching or not decoding at the ESC.
