Provisioning Wi‑Fi using the `wifi_prov` example
------------------------------------------------

Overview
- The `wifi_prov` example (from furyhawk/idf-extra-components) is copied into `examples/wifi_prov`.
- Build and flash this example to provision Wi‑Fi credentials (SoftAP or BLE). The credentials are stored in NVS and can be used by your main application.

Quick steps (macOS / Linux)

1. Open a terminal and source the ESP-IDF environment (example):

   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

2. Build, flash and monitor the example from the project root:

   ```bash
   cd examples/wifi_prov
   idf.py set-target esp32s3
   idf.py menuconfig   # optional: enable SoftAP / BLE transport and security options
   idf.py build flash monitor
   ```

3. Follow the example output to start provisioning. For SoftAP mode the device will start an AP whose SSID is `PROV_xxxxxx`.
   The example prints a QR code / provisioning URL you can use with the Espressif provisioning app.

Notes
- The example writes Wi‑Fi credentials to NVS in the default partition. After successful provisioning, your main application should be able to use those saved credentials and connect automatically.
- If your main application uses a different partition table, ensure `examples/wifi_prov/partitions.csv` is compatible.
- You can enable `CONFIG_EXAMPLE_PROV_TRANSPORT_SOFTAP` or `CONFIG_EXAMPLE_PROV_TRANSPORT_BLE` in `menuconfig` prior to building.

Files added
- `components/network_provisioning/` — provisioning component
- `examples/wifi_prov/` — standalone example app to run the provisioning flow

If you want, I can:
- Enable provisioning inside your `07_Matrix_WiFi` module so provisioning runs from the tab UI.
- Run a local build for the example (if ESP-IDF is configured in this environment).
