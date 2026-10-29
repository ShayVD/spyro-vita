# Spyro the Dragon → PS Vita: project setup

The plan: build on the **TheMobyCollective/spyro-1** decomp (CC0) and **PsyCross** (MIT), and convert the unmatched functions to C. Render with vitaGL and SDL2. Don't use OpenPete's code: it's closed source and licensed noncommercial.

## 1. Before writing any code (≈30 min)

Send these three messages first. The answers can change the scope.

- **Bounty poster** (zealouschuck.com, via the Ko-Fi comment or contact page). Ask whether a native Vita port built on the Spyro 1 decomp counts for the "Spyro 1 Decomp (Open Pete)" bounty, or whether it must be OpenPete itself. Also ask about requirements like a 60 fps minimum, an AI policy or physical button support.
- **tyscorp** (OpenPete). Ask whether anyone is working on an ARM or Vita build, whether the source is coming soon, and whether a renderer abstraction is planned.
- **altro50** (the decomp). Tell them about the Vita port plan. Ask whether there are non-matching C drafts for the remaining `pete/`, `camera/` and `cyclorama` functions, for example decomp.me scratches.

## 2. Toolchain (macOS host)

```bash
# VitaSDK
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH
git clone https://github.com/vitasdk/vdpm && cd vdpm && ./bootstrap-vitasdk.sh && ./install-all.sh
```

Then:

- **vitaGL:** build from source with the flags your KH ports use (for example `HAVE_GLSL_SUPPORT=1`), or copy the setup from `~/Projects/kh-bbs-vita/third_party`.
- **SDL2 for Vita:** already included in the vdpm packages.
- **Docker:** needed for the decomp's matching build (`docker_env.sh`) and for a 32-bit Linux debug build.
- **CMake 3.20+, Ninja, Python 3.**
- **Vita3K:** the emulator, for a fast test loop. Real hardware always has the final say.
- **On the Vita:** HENkaku/Enso, VitaShell, vitacompanion (FTP plus a command port for deploying) and PrincessLog (or `sceClibPrintf` to a socket) for logs.

## 3. Repo skeleton

**Done:** this repo was created with the commands below. `psxrecomp-ref` was cloned shallow to `~/Projects/psxrecomp-ref`.

```bash
mkdir -p ~/Projects/spyro-vita && cd ~/Projects/spyro-vita
git init
git submodule add https://github.com/TheMobyCollective/spyro-1 third_party/spyro-1
git submodule add https://github.com/OpenDriver2/PsyCross third_party/PsyCross
# reference only — licence unclear, do NOT vendor or copy code from it
git clone https://github.com/RetroPortingToolKit/psxrecomp ../psxrecomp-ref
mkdir -p docs src/{game,platform/vita,platform/host} tools patches disc logs
```

The layout this produces:

```
spyro-vita/
  CLAUDE.md            ← project instructions (this folder's CLAUDE.md)
  docs/                ← research.md, porting-plan.md, decisions.md, logs of findings
  third_party/spyro-1  ← decomp submodule — never edit in place; changes go in patches/
  third_party/PsyCross ← PsyQ reimplementation
  patches/             ← our changes to submodules (git format-patch / quilt style)
  src/game/            ← C translations of unmatched asm, port-only shims
  src/platform/vita/   ← vitaGL renderer, input, audio, file I/O, main
  src/platform/host/   ← 32-bit Linux debug build (Docker, -m32)
  tools/               ← asm→C converter, disc extractor wrappers, deploy scripts
  disc/                ← user's own extracted disc files (gitignored)
  logs/                ← run logs/screens (gitignored)
```

Write the `.gitignore` before adding anything else:

```gitignore
disc/
logs/
build*/
*.bin
*.cue
*.iso
*.img
WAD.WAD
PSX.EXE
*.vpk
```

## 4. Game data (from your own disc only)

1. Dump your own **NTSC-U disc, `SCUS-94228`**, to bin/cue.
2. Extract the files with `dumpsxiso` from [mkpsxiso](https://github.com/Lameguy64/mkpsxiso) into `disc/`.
3. Check that `PSX.EXE` matches the hash the decomp targets:
   - SHA-1 `84E3728AB94720D0873E2514ADF4AADE4935E0C5`
   - SHA-256 `A533D75CAB8AFAAE6107EC35A02A9A5FE979A92C7C955F9CF1EE50F693A1B998`

   ```bash
   shasum -a 1 disc/PSX.EXE
   ```
4. Never commit anything from `disc/`. The runtime reads `WAD.WAD` and friends from `ux0:data/spyro/` on the Vita.

## 5. Sanity-check the decomp

```bash
cd third_party/spyro-1 && ./docker_env.sh
# inside the container:
make all -j8
sha1sum build/PSX.EXE   # must equal the hash above
```

If it doesn't match, fix that first. The matching build is how you'll check every symbol and data layout later.

## 6. Hook up Claude

**Claude Code (recommended, since it can run builds and deploy):**
1. Copy `CLAUDE.md` from this folder to the repo root.
2. Put the research summary in `docs/research.md`.
3. Open the folder in the Code tab and paste `FIRST_PROMPT.md`.

**claude.ai Project (planning and chat only):**
1. Paste `CLAUDE.md` into the Project instructions.
2. Upload as knowledge: `docs/research.md`, the spyro-1 README, the PsyCross README, and any porting notes or docs from the KH1 decomp, KH BBS Vita and Duke Nukem Advance ports.
