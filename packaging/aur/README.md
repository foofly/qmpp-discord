# AUR packaging

Recipe for the `qmmp-discord-rich-presence` package on the [Arch User Repository](https://aur.archlinux.org/).

## Test the build locally

```bash
cd packaging/aur
updpkgsums            # fills sha256sums (needs pacman-contrib)
makepkg -sf           # builds the package
namcap PKGBUILD *.pkg.tar.zst
sudo pacman -U qmmp-discord-rich-presence-*.pkg.tar.zst
```

## Bump for a new release

1. Edit `pkgver` to the new tag, reset `pkgrel=1`.
2. `updpkgsums`
3. `makepkg --printsrcinfo > .SRCINFO`
4. Commit the updated `PKGBUILD` and `.SRCINFO`, then push to AUR.

## First-time AUR submission

```bash
git clone ssh://aur@aur.archlinux.org/qmmp-discord-rich-presence.git aur-repo
cp PKGBUILD .SRCINFO aur-repo/
cd aur-repo
git add PKGBUILD .SRCINFO
git commit -m "Initial import: 1.0.0-1"
git push
```
