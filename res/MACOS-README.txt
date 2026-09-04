Roguelike Cardgame - macOS
==========================

If macOS says the file "is not safe" and only offers Done or Move to Trash,
the game is not broken and nothing is wrong with your download.

macOS quarantines anything downloaded from the internet that has not been
signed with a paid Apple Developer ID and submitted to Apple for notarisation.
This is a hobby project and has not been through that process, so Gatekeeper
refuses to open it rather than warning you.

FIX (choose either)

1. Terminal, one line. Open Terminal, type the following with a trailing
   space, drag the app onto the window so the path fills itself in, press
   Return:

       xattr -dr com.apple.quarantine 

   Then open the app normally.

2. System Settings. Try to open the app once and let it be blocked. Then go to
   System Settings > Privacy & Security, scroll to the Security section, and
   click "Open Anyway" next to the message about Roguelike Cardgame. Confirm
   on the next prompt.

   On macOS 14 and earlier you can instead right-click (or Control-click) the
   app and choose Open, which offers an Open button the plain double-click
   does not.

WHY NOT JUST FIX IT PROPERLY

Removing the warning needs an Apple Developer ID (99 USD/year) and each
release run through Apple's notarisation service. If that ever happens, this
file goes away.

The app keeps its assets, config and sounds inside the bundle next to the
executable. Do not move the executable out on its own - it looks for those
folders beside itself.
