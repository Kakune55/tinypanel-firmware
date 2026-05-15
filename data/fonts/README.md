# Unicode Font Data

Place the generated `unicode16.uf16` file here before uploading LittleFS:

```sh
python3 tools/build_unicode16_font.py assets/fonts/wenquanyi_12pt.bdf data/fonts/unicode16.uf16
pio run -t uploadfs
```

The firmware reads the file from `/fonts/unicode16.uf16`. If the file is missing
or invalid, non-ASCII characters render as the missing-glyph box.
