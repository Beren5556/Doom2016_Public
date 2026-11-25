# Verification

Original engine and Neural processing were validated in-game by Carlos on 27 October 2026. The engine is unchanged in this portable distribution. Original offline checks comprised 17 verification groups including 16 engine CTest regressions. Neural remains optional in the public package and its proprietary model is provided separately by the user.

The portable package was checked without launching the game. Its private Python/Pillow and Node/Playwright run successfully, runtime file hashes match, and the first-launch setup relocates paths. The full archive passes CRC verification. Startup scripts prepare paths and open KharvoxLauncher.exe. The public portable edition itself still needs a first game/headset test.
