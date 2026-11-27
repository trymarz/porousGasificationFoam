# Testing a fresh install

[Installing](install.md) is written for a new user on a clean Ubuntu. On a developer's machine, OpenFOAM, YADE, conda, `~/.local` and environment variables already exist and can quietly fill gaps in the instructions. `container/pgf-fresh.sh` gives you a clean Ubuntu 26.04 instead, in a rootless Podman container, to walk through the install page from step 1 and see what a newcomer sees.

```
 host (your machine)                         container "pgf-fresh" (Ubuntu 26.04)
 ┌───────────────────────────┐               ┌─────────────────────────────────────┐
 │ ~/pgf-fresh-home  ────────┼── mounted as ─┼─► /home/ubuntu  (user ubuntu, sudo)  │
 │ /tmp/.X11-unix    ────────┼── read-only ──┼─► GUI windows open on your desktop   │
 │ everything else: home,    │               │ own /usr, /etc, /opt, /tmp           │
 │ dotfiles, OpenFOAM, env   │   not shared  │ no host environment variables        │
 └───────────────────────────┘               └─────────────────────────────────────┘
```

## Use it

You need Podman on the host (`sudo apt-get install podman` on Debian or Ubuntu; rootless, so your user needs `/etc/subuid` and `/etc/subgid` entries, which a normal account has). Then, from the checkout:

```bash
container/pgf-fresh.sh create    # pull ubuntu:26.04, make the container and ~/pgf-fresh-home
container/pgf-fresh.sh shell     # a login shell inside, as user ubuntu
```

Inside, follow [Installing](install.md) from step 1, as written: `sudo apt-get …`, the clone, `./pgf setup`, `pgf build`. `sudo` there has no password and changes only the container. Every command, the settings (`PGF_FRESH_NAME`, `PGF_FRESH_IMAGE`, `PGF_FRESH_HOME`) and what is shared: the header of `container/pgf-fresh.sh`, or `container/pgf-fresh.sh help`.

What lands where:

```
~/pgf-fresh-home/            (host)  = /home/ubuntu (container)
├── porousGasificationFoam/  your clone, from GitHub: only pushed branches exist here
├── .local/bin/pgf           linked by pgf setup
├── .local/opt/yade-pgf/     YADE, if you do step 3
└── .cache/                  YADE build tree
container only (gone with rm): /usr/lib/openfoam/openfoam2606, apt packages, /tmp (pgf's run root)
```

## Reset

```bash
container/pgf-fresh.sh rm            # drop the system side (OpenFOAM, apt packages); keep the home
container/pgf-fresh.sh rm --home     # also delete ~/pgf-fresh-home: a brand-new user (asks first)
container/pgf-fresh.sh create        # start again
```

Removing both leaves the host as it was, apart from Podman's downloaded image (`podman rmi ubuntu:26.04` drops it).

To skip a long step on later runs, save the container after it as an image, for example once OpenFOAM is in: `podman container commit pgf-fresh localhost/pgf-fresh:openfoam`, then `PGF_FRESH_IMAGE=localhost/pgf-fresh:openfoam container/pgf-fresh.sh create`. The image holds the system side only; the home is kept separately.

## Limits

- It tests the instructions, not the machine: the kernel, CPU count, GPU driver and display server are the host's.
- GUI programs use X11 (XWayland on a Wayland desktop) through the host's `xauth` cookie, copied in by `shell` and `run`.
- The clone comes from GitHub, so test a branch by pushing it first, then `git fetch && git switch <branch>` inside.
- [Apptainer images](containers.md) are the other container in PGF: they run cases on a server, and start from an image that already has OpenFOAM.

## Related pages

- [Installing](install.md): the page this tests.
- [Clean rebuild and uninstall](uninstall.md): removing PGF from a real machine.
- [All documentation](README.md)
