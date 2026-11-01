# Halcyon Wander

A generative instrument for the Teenage Engineering OP–XY, built with the
unofficial [Open-XY SDK](https://github.com/etcetc000/open-xy). Hold a key and
Halcyon's soft tones wander in a scale of their own, from the key you hold. After Mutable Instruments' Marbles. By Colors In Space.

| Page 1 | |
| --- | --- |
| **Sound** | fifteen Halcyon sounds by Colors In Space, CIS 1 to CIS 15 |
| **Rate** | how often the line steps, from every 4 s to 20 a second |
| **Chance** | how many of those steps sound |
| **Scale** | minor, dorian, harmonic minor, phrygian, lydian, major, major pentatonic, minor pentatonic |

| Page 2 (press) | |
| --- | --- |
| **Deja vu** | as on Marbles: 0 always new notes, full loops the last Length steps |
| **Length** | the looped phrase, 1 to 16 steps |
| **Spread** | how far it roams: 1 to 4 octaves |
| **Jitter** | loosens the timing: each step lands up to a quarter step early or late |

The screen is an orrery: the key you hold is the sun, each ring an octave, the
scale's notes lit at their clock positions, and a marble landing on each note as it
sounds. Screen and sound follow the same line
(`journey.h`), so they stay in step without hearing each other; with a chord held,
the screen follows the first key, and every key plays the same line from its own root.

The first note is always the key you hold. Hold a chord and each key wanders on
its own. The amp envelope's release sets how long a line keeps going after you let go.

Each note is a whole Halcyon voice at its own pitch, swelling in and fading over about four seconds; up to four ring at once.

## Licence

MIT, see [LICENSE](LICENSE).
