# TARS TODO

项目级待办（Agent / 维护者）。协议真源见 `tars-io-mux` / `tars-tnb-lite` 的 `docs/tars-node-bus.md`。

---

## Node Bus

- [x] **地址分段扩容（LITE / FULL / RSV）** — `0`+6bit → LITE `[0x00,0x40)`；`10`+5bit → FULL `[0x40,0x60)`；`11`+5bit → RSV `[0x60,0x80)`（含 ARP `0x61`）。LITE 实务烧录 `0x10..0x3F`（board_id 0..47）；FULL 首选/ARP `0x40..0x5F`。见 `tnb_protocol.h` / `tars-node-bus.md` §1.1。
  - **策略**：只认新池；master 扫描按 PROFILE 校验（LITE 段拒 FULL，FULL 段拒 LITE）。不向后兼容旧 FULL 占 `0x10..0x2F`。
  - **短期（硬件，可选）**：多 I²C / TCA9548A — 单段仍受 7-bit 与电容限制。
  - **长期（TNB v2）**：逻辑 `node_id` 与 7-bit 地址彻底解耦；桥接/多级路由。
