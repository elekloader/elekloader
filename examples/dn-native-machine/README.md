# Native SYN machine (Digitone mk1, OS 1.44)

A sine with FINE on SYN1's second page (`DNM_PAGES2`), a native drawing
callback, and a full label for notifications and LFO DEST. SYN1 keeps two
pages; SYN2 has one. The companion handles the held-lock value, unused
knobs and FUNC+turn defaults on both pages. Machine id 3 must be free.

```sh
python -m elekloader.sdk.build mods/core-dn1 --stock stock.syx --out build/core
python -m elekloader.sdk.build mods/machines-dn1 --stock stock.syx --out build/machines
python -m elekloader.sdk.build examples/dn-native-machine --stock stock.syx --out build/native
python -m elekloader.lint build/native/dnnative-1.0.elemod --stock stock.syx --with build/core/core-4.2-os1.44.elemod --with build/machines/machines-0.3-os1.44.elemod
```

Select NATIVE SINE in MACHINES. Turn SYN1 2/2 A: the display reads FINE,
the notification and LFO list read Osc2 Fine, and the current DEST reads
FINE. Hold a trig to see its lock; FUNC+turn restores 64. The descriptor
contract is in [the SDK header](../../elekloader/sdk/include/digitone-mk1/machines.h)
and [Machines on the Digitone](../../docs/ADAPTING.md#machines-on-the-digitone-machines-digitone-mk1).
