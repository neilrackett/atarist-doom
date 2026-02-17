# DOOM for Atari ST, TT & Falcon

Ported by [Neil Rackett](https://x.com/neilrackett)

<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/82745b03-d139-4fbc-a829-4db99578a9e0" /> 
<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/d613814a-4ba7-4dde-946a-a4f457d341c4" />

_Atari TT & Falcon_

<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/d92262b5-f98f-4a7d-89fb-5bae89ad24ee" /> 
<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/75389c0f-416c-4496-95b9-83fc137c19ce" />

_Atari ST_

Welcome to my experimental SDL DOOM port: it looks great on any Atari ST compatible computer, but is only really playable on a TT or Falcon (or with a fast CPU setting in [Hatari](https://www.hatari-emu.org/))

- Runs in greyscale on ST, 256 colours in TT & Falcon
- Support keyboard, mouse and joystick controls
- Automatically switches to 16Mhz with cache mode on Mega STE
- Sound effects should work, but music is still WIP

## Installing

- Build or [download](https://github.com/neilrackett/atarist-doom/releases) `DOOM.TOS` (any ST compatible) and/or `DOOM_030.TOS` (TT & Falcon only)
- Copy the TOS files to your hard disk alongside `DOOM1.WAD`
- Run `DOOM.TOS` or `DOOM_030.TOS`

## Building

- Install [atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker)
- Run `stcmd make`
- `DOOM.TOS` and `DOOM_030.TOS` will be output to the `build` folder

## Credits

Big thank you to [doomgeneric](https://github.com/ozkl/doomgeneric)
