# Digitone parameter presentation

A core-only consumer of the shared names/notification/LFO interface on OS1.44.
It labels AMP attack as `Attack` / `ATK`, formats its raw integer knob value
in notifications, and leaves destination visibility to native defaults except
for explicitly showing attack. The formatter is an example of raw8.8 display,
not a conversion to envelope time. It needs core4.2 and no machines companion.

```sh
python -m elekloader.sdk.build examples/dn-param-presentation --stock Digitone_and_Digitone_Keys_OS1.44.syx
```

The API and callback contracts are in [docs/ADAPTING.md](../../docs/ADAPTING.md#persistent-sound-edits-and-held-steps-core-dn1-42-digitone-mk1).
