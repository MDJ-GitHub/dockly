# Dockium welcome: new slides + slide animations

Copy into chrome/browser/resources/dockium_welcome/ :
  welcome.js                 (replaces yours; no CSS changes are needed any more)
  slide4.png .. slide7.png   (placeholders: replace with your own pictures)

In BUILD.gn of that folder, add after "slide3.png":
    "slide4.png",
    "slide5.png",
    "slide6.png",
    "slide7.png",

If you appended the earlier slide-transition CSS to welcome.css, you can
leave it or delete it: the animations now run from welcome.js.
