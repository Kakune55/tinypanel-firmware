# Font Sources

Put source BDF fonts here. For the 16x16 Unicode font pipeline, use the 12pt
WenQuanYi bitmap BDF from `larryli/u8g2_wqy`:

```text
assets/fonts/wenquanyi_12pt.bdf
```

The generated LittleFS font is not derived from the existing GB2312 map. It is
indexed directly by Unicode codepoint.
