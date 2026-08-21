# Title ID / save-file matrix and conversion

Source of truth: [games.json](games.json) (copied into the server at runtime). Console clients use [clients/shared/games.h](../clients/shared/games.h).

| id | game | platform | title ID | save files | PKHeX format |
|----|------|----------|----------|------------|--------------|
| x | X | 3DS | `0004000000055D00` | `main` | 6 |
| y | Y | 3DS | `0004000000055E00` | `main` | 6 |
| or | Omega Ruby | 3DS | `000400000011C400` | `main` | 6 |
| as | Alpha Sapphire | 3DS | `000400000011C500` | `main` | 6 |
| sun | Sun | 3DS | `0004000000164800` | `main` | 7 |
| moon | Moon | 3DS | `0004000000175E00` | `main` | 7 |
| us | Ultra Sun | 3DS | `00040000001B5000` | `main` | 7 |
| um | Ultra Moon | 3DS | `00040000001B5100` | `main` | 7 |
| red | Red (VC) | 3DS | `0004000000171000` (+ regions) | `sav.dat` (extdata) | 1 |
| blue | Blue (VC) | 3DS | `0004000000171100` (+ regions) | `sav.dat` (extdata) | 1 |
| yellow | Yellow (VC) | 3DS | `0004000000171200` (+ regions) | `sav.dat` (extdata) | 1 |
| green | Green (VC) | 3DS | `0004000000170D00` (JPN) | `sav.dat` (extdata) | 1 |
| gold | Gold (VC) | 3DS | `0004000000172600` (+ regions) | `sav.dat` (extdata) | 2 |
| silver | Silver (VC) | 3DS | `0004000000172700` (+ regions) | `sav.dat` (extdata) | 2 |
| crystal | Crystal (VC) | 3DS | `0004000000172800` (+ regions) | `sav.dat` (extdata) | 2 |
| dp | Diamond | DS cart | `DS-ADA` | SPI flash | 4 |
| pearl | Pearl | DS cart | `DS-APA` | SPI flash | 4 |
| pt | Platinum | DS cart | `DS-CPU` | SPI flash | 4 |
| hg | HeartGold | DS cart | `DS-IPK` | SPI flash (IR) | 4 |
| ss | SoulSilver | DS cart | `DS-IPG` | SPI flash (IR) | 4 |
| bl | Black | DS cart | `DS-IRB` | SPI flash | 5 |
| wh | White | DS cart | `DS-IRA` | SPI flash | 5 |
| b2 | Black 2 | DS cart | `DS-IRE` | SPI flash (IR) | 5 |
| w2 | White 2 | DS cart | `DS-IRD` | SPI flash (IR) | 5 |
| sw | Sword | Switch | `0100ABF008968000` | `main` (+ `backup`) | 8 |
| sh | Shield | Switch | `01008DB008C2C000` | `main` (+ `backup`) | 8 |
| bd | Brilliant Diamond | Switch | `0100000011D90000` | `main` | 8 |
| sp | Shining Pearl | Switch | `010018E011D92000` | `main` | 8 |
| pla | Legends Arceus | Switch | `01001F5010DFA000` | `main` | 8 |
| sl | Scarlet | Switch | `0100A3D008C5C000` | `main` (+ `backup`) | 9 |
| vl | Violet | Switch | `01008F600124C000` | `main` (+ `backup`) | 9 |

The 3DS client mounts Gen 6–7 from USER_SAVEDATA (`main`), official GB/GBC Virtual Console from extra data (`sav.dat`, all regional title IDs), and Gen 4–5 from an inserted DS cart’s save chip. PKHeX identifies the SAV variant from the file bytes.

## Destination conversion (Bank / Transporter / Home-like)

`EntityConverter.IsConvertibleToFormat` plus destination `MaxSpeciesID`:

- Forward (1/2 → 4/5 → 6/7 → 8 → 9) is allowed when PKHeX has a route.
- Backward (9 → 7, 9 → 6, 7 → 4, …) is rejected (`NoTransferRoute`).
- Species above the destination dex cap cannot be withdrawn.

Covered by `tests/GameMatrixTests.cs`.
