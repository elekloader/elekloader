# splash

The code of the mod the web page's **Splash screen maker** makes: a picture or
an animation over the boot intro of the Digitakt mk1 (1.53, 1.54) and the
Digitone mk1 / Keys (1.43, 1.44).

There is nothing to build here. `elekloader/splash.py` keeps this file's
bytes, assembled, and fills in the five addresses each OS needs, so the page
makes the mod without a toolchain; `tests/test_units.py` assembles
`splash.s` for each OS and checks the bytes match. From Python:

```python
from elekloader import splash
frame = splash.pack(rows)            # 64 rows of 128 pixels -> 1024 bytes
doc = splash.make([frame], device='digitakt-mk1', os='1.54', title='My logo')
open(splash.file_name('digitakt-mk1', '1.54'), 'wb').write(splash.dumps(doc))
```

How it works, the addresses, and what was checked: [docs/SPLASH.md](../../docs/SPLASH.md).
