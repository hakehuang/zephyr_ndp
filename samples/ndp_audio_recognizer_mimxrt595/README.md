# NDP audio recognition sample (MIMXRT595, CM33)

This sample demonstrates an **NDP** audio pipeline aligned with the design in `doc/ARCHITECTURE.md`:

- `MicSource` (producer) → `FeatureExtract` (DSP-ish transform) → `Recognizer` → `TextSink`

It is written to run on the **CM33** and uses a *synthetic mic* by default (so it runs even without board audio plumbing).

## Build

From a Zephyr workspace:

- Ensure the NDP module is discoverable, e.g.:
  - set `ZEPHYR_EXTRA_MODULES` to point at `.../zephyr_ndp/modules/ndp`, or
  - add it to your west manifest.

Example (Windows PowerShell):

- `setx ZEPHYR_EXTRA_MODULES "C:\github\zephyr_ndp\modules\ndp"`

Then build:

- `west build -b mimxrt595_evk -p auto samples/ndp_audio_recognizer_mimxrt595`

## Notes about DSP-core offload (HiFi4)

The RT595 has a HiFi4 DSP core. A real offload setup typically uses **OpenAMP/RPMsg** or NXP-specific IPC.
This sample keeps the pipeline structure compatible with offload, but the provided code runs entirely on CM33.

To turn this into a true CM33↔DSP pipeline:

- run `MicSource` + framing on CM33,
- send frames/features over RPMsg to a DSP firmware that runs the recognizer,
- return results (keyword IDs / text) back to CM33.

If you want, tell me which IPC stack you’re using on RT595 (OpenAMP/RPMsg-Lite/mailbox), and I’ll extend the module with an AMP channel implementation + a paired DSP-side skeleton.
