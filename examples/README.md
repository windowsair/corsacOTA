## Flash

The default flash size is 4MB, for other sizes, modify `sdkconfig.default`:

```
# Choose a flash size.
CONFIG_ESPTOOLPY_FLASHSIZE_1MB=y
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# Then set a flash size
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
```

The flash size of the board can be checked with the following command:
```bash
esptool.py -p (PORT) flash_id
```

For devices with 1MB flash size such as ESP8285, the following changes are also required:

```
CONFIG_PARTITION_TABLE_FILENAME="partitions_two_ota.1MB.csv"
CONFIG_ESPTOOLPY_FLASHSIZE_1MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="1MB"
CONFIG_ESP8266_BOOT_COPY_APP=y
```

***Please take care to ensure that the 1MB device's OTA data is valid, otherwise it may not boot properly.*** For more information, refer to: https://github.com/espressif/ESP8266_RTOS_SDK/issues/745


## Build

```bash
idf.py build
```