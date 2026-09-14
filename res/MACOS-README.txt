Roguelike Cardgame on macOS
===========================

If the app will not open, that is Gatekeeper, not a broken download.

The game is signed, but only ad-hoc: proper notarisation needs a paid Apple
Developer account. macOS treats an un-notarised app downloaded from the
internet as untrusted and refuses to launch it.

macOS 15 (Sequoia) and newer
----------------------------
Right-click > Open NO LONGER works on Sequoia. Apple removed that bypass.
Do this instead:

  1. Double-click the app once. It will be blocked - that is expected.
  2. Open System Settings > Privacy & Security.
  3. Scroll to Security. There will be a line naming the app and an
     "Open Anyway" button. Click it.
  4. Confirm with Touch ID or your password.

The app opens normally from then on.

macOS 14 (Sonoma) and earlier
-----------------------------
  1. Right-click (or Control-click) the app.
  2. Choose Open.
  3. Click Open in the dialog.

The one-line alternative
------------------------
If you are comfortable in Terminal, clearing the quarantine flag skips all of
the above. Replace the path with wherever you put the app:

  xattr -dr com.apple.quarantine "/Applications/Roguelike Cardgame.app"

Then launch it normally.

"The application is damaged and can't be opened"
------------------------------------------------
That message usually means the archive was unpacked by something that dropped
the bundle's structure or the executable bit. Unpack the .tar.gz with Finder or
with `tar -xzf`, not with a third-party unarchiver.

Requirements
------------
macOS 11 (Big Sur) or newer, Apple Silicon. The release is built on GitHub's
arm64 runner, so it will not run on an Intel Mac.
