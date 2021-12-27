## Flash

The default flash size is 4MB, for other sizes, modify `sdkconfig.default`

The flash size of the board can be checked with the following command:
```bash
esptool.py -p (PORT) flash_id
```

## Build

```bash
idf.py build
```