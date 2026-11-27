Recorded screens for tests/CharonPixelControlsTests.cpp (r410).

Copies of captures from the r410 corpus (QEMU/OVMF screen dumps, binary PPM):

  refind-00-start.ppm     rEFInd 0.14.2, 1280x800. Three large OS icons in a row, the FIRST
                          on a grey plate (selected); below them six small tool icons, none
                          selected; a line "Boot EFI\probe\GPROBE.EFI from GRUBTEST" under them.
  refind-01-right.ppm     the same after Right: the plate is under the SECOND OS icon.
  clover-00-start.ppm     Clover, 1600x900. One OS entry (Windows) on a grey plate (selected);
                          below it five small tool icons, none selected; pointer arrow at 400,228.
  clover-01-right.ppm     after Right: the OS entry has no plate; the FIRST tool icon has one.
  canopy-00-start.ppm     OpenCanopy, 1280x800. Three volume icons with captions CHARON,
                          GRUBTEST, Windows under them; the FIRST on a grey plate; two round
                          buttons at the bottom; pointer arrow at 644,511.
  canopy-01-right.ppm     after Right: the plate is under the SECOND icon (GRUBTEST).
  winmenu-00-start.ppm    Windows Boot Manager, 1280x800: a light bar over "Windows Setup",
                          the first of two entries; a title bar and a key bar.
  grubtheme-00-start.ppm  GRUB with a theme, 800x600: a photograph as the background.

Recorded screens for tests/CharonAreaOcrTests.cpp (r410, the reader's text path; it also uses
grubtheme-00-start.ppm and clover-00-start.ppm from the list above):

  grubtheme-00-start.ppm  (above) its countdown label "The highlighted entry will be executed
                          automatically in 3s." stands in a progress bar, black on white left of
                          column 467 and on 0x66AACC right of it: the bar's edge crosses the "u" of
                          "automatically". Bar: rows 480-507, columns 119-677.
  grubtheme-04-e.ppm      the same theme's entry editor, 800x600: GRUB's terminal in a box of dashed
                          lines, its font spaced out in cells 12 pixels wide, lines 15 pixels apart;
                          lines "setparams 'Charon test entry two'" (underline caret under the s),
                          'echo "Entry two selected"', "sleep 3"; a title and four help lines. A
                          5-pixel strip of the wallpaper with a dim border line on both sides.
  grubthemepl-02-menu.ppm the theme's text menu after "configfile" (lines 18 pixels apart), Polish
                          entries, the first one chosen ("*Zażółć gęślą jaźń"), a countdown of 20 s.
  grubthemepl-03-down.ppm the same menu after Down: the second entry chosen, black on a grey bar
                          ("*Opcje zaawansowane: źródła i ścieżki"); the tails of the first entry's
                          letters reach down to the bar.
  grubthemepl-06-e.ppm    its entry editor ("setparams 'Opcje zaawansowane: źródła i ścieżki\",
                          menuentry, linux, initrd lines), underline caret under the first s.
  grubthemepl-01-c.ppm    its command line: five lines of help text, no box.
  clover-font.png         Clover 5176's embedded console font image (176x288, one bit per pixel,
                          16x16 glyph cells; BSD 2-Clause as Clover itself), as the reader finds it
                          inside CLOVERX64.efi. Needed to read clover-00-start.ppm, where
                          "5176 GRUBTEST\EFI\CLOVER" stands at the right edge of the row of "F1:Help".

The expected lines of that test were read off the pictures by eye (the same lines as in
tests/charon-screens/ocr-reference.txt where the screen is listed there).

The expected values in the control test (counts, which one is selected, captions) were read off the
pictures by eye. The text lines the test passes in stand for what the reader's OCR supplies;
their rectangles were measured on the pictures.
