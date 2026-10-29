# Persistent Sound edit (Digitone mk1, OS1.44)

The smallest consumer of `core_sound_set`, without the machines companion.
Hold a synth track key and select SET ATTACK: AMP attack becomes32 and the
native packed Sound cache is invalidated, so ordinary save/shutdown can keep
it. Project saving still follows the native workflow.

```sh
python -m elekloader.sdk.build examples/dn-sound-edit --stock stock.syx --out build/edit
python -m elekloader.lint build/edit/dnsoundedit-1.0.elemod --stock stock.syx --with build/core/core-4.2-os1.44.elemod
```

Needs core-dn1 4.2 on OS1.44. The SDK test builds/links it without machines
and refuses core4.1.1. See [Persistent Sound edits and held steps](../../docs/ADAPTING.md#persistent-sound-edits-and-held-steps-core-dn1-42-digitone-mk1).
