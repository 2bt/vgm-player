# VGM Player

This is a simple VGM player.
It plays VGM and VGZ files which can be downloaded at [vgmrips.net](https://vgmrips.net).
Currently, the following chips are supported:

+ GA20
+ RF5C68
+ YM2151
+ YM2203
+ YM2612
+ LR35902 (Game Boy)

The Yamaha sound chips are emulated via [ymfm](https://github.com/aaronsgiles/ymfm).

For the YM2203, there is also an alternative implementation which can be enabled via `-s`.
It is not trying to be super accurate, but it sounds not too bad IMO and the code is very simple.
I gave each voice a different panning to make it sound more interesting.
Check out the code [here](src/ym2203.hpp).

There are some really great soundtracks out there for the YM2203, e.g.
[EVE Burst Error](https://vgmrips.net/packs/pack/eve-burst-error-nec-pc-9801),
[Xenon: Mugen no Shitai](https://vgmrips.net/packs/pack/xenon-mugen-no-shitai-nec-pc-9801),
[Sorcerian](https://vgmrips.net/packs/pack/sorcerian-nec-pc-8801),
[Ys](https://vgmrips.net/packs/pack/ys-ancient-ys-vanished-omen-nec-pc-8801), and
[Ys II](https://vgmrips.net/packs/pack/ys-ii-ancient-ys-vanished-the-final-chapter-nec-pc-8801).
