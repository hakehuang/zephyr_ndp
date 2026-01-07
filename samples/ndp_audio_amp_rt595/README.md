# NDP audio recognition AMP sample (i.MX RT595 CM33 ↔ DSP)

This sample splits the audio pipeline across two domains using **OpenAMP/RPMsg** for notifications:

- **CM33 (MCU domain)**: captures audio frames (I2S/SAI) → writes PCM blocks into **shared memory** → enqueues a message into an AMP shared-memory ring → sends an **OpenAMP doorbell** (`ipc_service_send`) to wake DSP.
- **DSP domain**: wakes on doorbell → dequeues PCM frames from shared memory ring → runs recognition → enqueues result message into return ring → sends a doorbell to wake CM33.

It uses the NDP module’s AMP shared-memory ring backend (`CONFIG_NDP_BACKEND_AMP=y`).

## Layout

- CM33 app: [samples/ndp_audio_amp_rt595/cm33](samples/ndp_audio_amp_rt595/cm33)
- DSP app: [samples/ndp_audio_amp_rt595/dsp](samples/ndp_audio_amp_rt595/dsp)

## Important notes

- **Shared memory address/size** and the **OpenAMP IPC service instance** are platform-specific. This sample expects you to provide them via Devicetree `chosen` nodes:
  - `chosen { ndp,shm = &ndp_shm0; ndp,ipc = &...; }`
- If your shared memory is cacheable, you must add cache maintenance (flush/invalidate) around ring/message and PCM writes.
- DSP-side Zephyr support depends on your RT595 software stack. If your DSP firmware is not Zephyr-based, you can still reuse the same shared-memory ring format and notifications.

If you share your exact RT595 OpenAMP setup (which node is the IPC service instance, memory map, and whether shared memory is cacheable), I can tailor the overlays and add the required cache maintenance calls.
