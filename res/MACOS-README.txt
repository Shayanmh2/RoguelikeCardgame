Moonstruck on macOS
===================

Runs on macOS 11 (Big Sur) or newer, on both Apple Silicon and Intel Macs.
It is one universal app: both versions are inside it, and macOS picks the
right one for your Mac on its own.

The easy way: install from Terminal
-----------------------------------
Paste this into Terminal and press Return:

  curl -fsSL https://github.com/Shayanmh2/RoguelikeCardgame/releases/latest/download/install-macos.sh | sh

It downloads the game, puts it in Applications and opens it, with no security
prompt. That is not a trick: macOS only checks apps that a web browser
downloaded, and a download made from Terminal is never marked that way.

If you downloaded the zip in a browser instead
----------------------------------------------
The first launch will be blocked. That is Gatekeeper, not a broken download.
The app is signed, but only ad-hoc: notarisation needs a paid Apple Developer
account, and macOS will not open an un-notarised app from the internet until
you tell it to once.

macOS 15 (Sequoia) and newer
----------------------------
Right-click > Open no longer works. Apple removed that route. Instead:

  1. Double-click Moonstruck once. It will be blocked. Click Done.
  2. Open System Settings > Privacy & Security.
  3. Scroll down to Security. There is a line naming Moonstruck with an
     "Open Anyway" button. Click it.
  4. Confirm with Touch ID or your password.

It opens normally from then on.

macOS 14 (Sonoma) and earlier
-----------------------------
  1. Right-click (or Control-click) Moonstruck.
  2. Choose Open.
  3. Click Open in the dialog.

The one-line alternative
------------------------
In Terminal, clearing the quarantine flag skips all of the above. Change the
path to wherever you put the app:

  xattr -dr com.apple.quarantine "/Applications/Moonstruck.app"

Then open it normally.

If macOS says the app is "damaged"
----------------------------------
Unzip it with Finder (double-click the .zip). Some third-party unarchivers drop
the bundle's executable bit or its signature. If it still says damaged, run the
xattr line above.

Where your saves are
--------------------
~/Library/Application Support/Moonstruck/

Saves live there rather than inside the app, so replacing the app with a newer
version keeps them.
