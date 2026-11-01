// Edit this list to change the slides.
// layout: "below" (text under image), "text-left", or "text-right"
// effect: "frame" (soft moving rainbow glow around the picture),
//         "edge"  (moving rainbow glow along the left edge of the window,
//                  with a strong burst every time you arrive on the slide),
//         "bottom" (the same glow, along the bottom edge of the window),
//         "logo"  (a bigger, stronger glow, for the final logo slide),
//         or leave it out for none.
// bare:   true = no panel or border behind the picture (for a logo).
// gap:    space (px) between the picture and the text on this slide, if you
//         want something other than the default.
// size:   how large the picture is, as a share of the free space (default 1;
//         for example 0.6 for a smaller logo).
// points: a list of bullet points shown under / beside the text.
// move:   how the slide travels when you arrive on it: "left", "right",
//         "up" or "down" (the stars in the background travel the same way).
//         Going back plays the same move in reverse. Leave it out for a
//         plain fade.
const slides = [
  { img: 'slide1.png', layout: 'below',      effect: 'frame', title: 'Welcome to Dockium!',
    body: 'The Ultimate Chromium Browser for Elite Power Users. Dock your world, isolate your accounts, and experience a background-first live updated workspace engineered to keep you endlessly connected.' },
  { img: 'slide2.png', layout: 'text-left',  effect: 'edge', move: 'left', title: 'Dock Everything.',
    body: 'Press + to dock your essential sites and social channels into dedicated session-isolated dock apps, keeping you connected to your loved ones and key workflows just one click away—always active in the background.' },
  { img: 'slide3.png', layout: 'text-right', effect: 'bottom', move: 'down', title: 'Out of Sight. But Never Out of Touch.',
    body: 'Never miss an important message when you close the browser, keeping your screen distraction-free. By staying active in your system tray, Dockium processes incoming alerts in real time and displays your total unread count right where you need it.' },
  { img: 'slide4.png', layout: 'text-left',  effect: 'frame', move: 'right', title: 'Reclaim your memory without losing your place.',
    body: 'Power Sleep uses a custom, aggressive discard engine to freeze inactive tab tasks, reducing RAM and CPU usage to virtually zero. Tabs remain in your bar as lightweight shells, preserving their full history and reloading instantly the moment you return.' },
  { img: 'slide5.png', layout: 'text-right', effect: 'frame', move: 'up', title: 'Block clutter everywhere. Effortlessly.',
    body: 'Enjoy distraction-free web apps with an adblocker powered by high-speed Rust performance. Whether you are browsing normally or working inside docked sidebar sessions, keep every page clean and fast with customizable domain-level controls.' },
  { img: 'slide6.png', layout: 'text-left',  effect: 'frame', move: 'left', title: 'Anonymous Browsing on Your Exact Terms.',
    body: 'Take complete control of your online presence with integrated Tor routing. Route your entire connection or assign specific proxy rules per site and docked app. Choose your preferred exit country, auto-route for optimal speed, or plug in your own custom VPN/proxy server—giving you elite-level privacy with zero extra cost.' },
  { img: 'slide7.png', layout: 'below',      effect: 'frame', move: 'down', title: 'Your Dock, Your Way.',
    body: 'Move the dock anywhere you like, then fine-tune its width and icon size until it feels exactly right.' },
  { img: 'logo.png', layout: 'below', effect: 'logo', bare: true, size: 0.6, gap: 90, move: 'up',
    body: 'We are excited for you to start using Dockium! More features are on the way, and because Dockium is still under development, please report any issue you find on our GitHub page. Have fun re-exploring the web!' },
];

let index = 0;
let lastEffect = '';
let busy = false;                      // true while a slide animation is running
const $ = (id) => document.getElementById(id);
const slideEl = $('slide');
const edgeEl = $('edge');

// Restart the strong "edge" flash (strong at first, fading out quickly).
function burstEdge() {
  if (edgeFade) { edgeFade.cancel(); edgeFade = null; }
  edgeEl.classList.remove('burst');
  void edgeEl.offsetWidth;            // force reflow so the animation restarts
  edgeEl.classList.add('burst');
}

// ---------- Bottom glow (slide effect "bottom") ----------
// The same kind of glow as the left edge, along the bottom of the window:
// strong at the very edge, fading out quickly, colors slowly moving, and a
// strong burst every time you arrive on the slide. It is built here with the
// element's own style, so it needs no CSS rules.
// The glow colors: richer than plain pastels (those blend to white), but
// still soft. Edit this list to change the colors of every glow.
const SPECTRUM = ['#ff5f6d', '#ff9a3c', '#f2c94c', '#34d399', '#38bdf8', '#7c8cff', '#c471ed'];
const GLOW_SPECTRUM = SPECTRUM.join(', ') + ', ' + SPECTRUM[0];
// The left-edge glow (welcome.css) reads this too.
document.documentElement.style.setProperty('--spectrum', GLOW_SPECTRUM);
const GLOW_FADE = 'linear-gradient(to top, rgba(0,0,0,0.85) 0%, rgba(0,0,0,0.35) 14%, transparent 100%)';
const GLOW_OPACITY = '0.8';
const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
const glowBottom = document.createElement('div');
glowBottom.setAttribute('aria-hidden', 'true');
Object.assign(glowBottom.style, {
  position: 'fixed',
  left: '0',
  right: '0',
  bottom: '0',
  height: '72px',
  pointerEvents: 'none',
  opacity: '0',
  transformOrigin: 'bottom',
  background: 'linear-gradient(to right, ' + GLOW_SPECTRUM + ')',
  backgroundSize: '200% 100%',
  webkitMaskImage: GLOW_FADE,
  maskImage: GLOW_FADE,
});
edgeEl.after(glowBottom);                       // same stacking position as the left glow
glowBottom.style.zIndex = getComputedStyle(edgeEl).zIndex;
if (!reduceMotion) {
  glowBottom.animate(
      [{ backgroundPosition: '0% 0%' }, { backgroundPosition: '100% 0%' }],
      { duration: 12000, iterations: Infinity, direction: 'alternate', easing: 'ease-in-out' });
}

const GLOW_FADE_OUT_MS = 500;                   // how long a glow takes to fade out
let bottomAnim = null;                          // the burst or fade-out that is running
let bottomOn = false;

// Arriving on a "bottom" slide: starts from nothing, swells quickly to a
// strong burst, then settles.
function showBottomGlow() {
  if (bottomOn) return;
  bottomOn = true;
  if (bottomAnim) bottomAnim.cancel();
  glowBottom.style.opacity = GLOW_OPACITY;      // where it settles
  if (reduceMotion) { bottomAnim = null; return; }
  bottomAnim = glowBottom.animate(
      [{ opacity: 0, transform: 'scaleY(0.6)', offset: 0, easing: 'ease-out' },
       { opacity: 1, transform: 'scaleY(2.4)', offset: 0.18, easing: 'cubic-bezier(0.1, 0.7, 0.2, 1)' },
       { opacity: Number(GLOW_OPACITY), transform: 'scaleY(1)', offset: 1 }],
      { duration: 5000 });
}

// Leaving it: fades out smoothly from however bright it is right now, even in
// the middle of the burst.
function hideBottomGlow() {
  if (!bottomOn) return;
  bottomOn = false;
  const cs = getComputedStyle(glowBottom);
  const fromOpacity = parseFloat(cs.opacity) || 0;
  const fromTransform = cs.transform === 'none' ? 'scaleY(1)' : cs.transform;
  if (bottomAnim) bottomAnim.cancel();
  glowBottom.style.opacity = '0';
  bottomAnim = null;
  if (reduceMotion || fromOpacity <= 0.01) return;
  bottomAnim = glowBottom.animate(
      [{ opacity: fromOpacity, transform: fromTransform },
       { opacity: 0, transform: 'scaleY(1)' }],
      { duration: GLOW_FADE_OUT_MS, easing: 'ease-out' });
}

// The left glow (slide 2) gets the same smooth fade-out.
let edgeFade = null;
function fadeOutEdge() {
  if (edgeFade) return;
  const cur = parseFloat(getComputedStyle(edgeEl).opacity) || 0;
  if (cur <= 0.01) return;
  edgeEl.classList.remove('burst');
  edgeFade = edgeEl.animate([{ opacity: cur }, { opacity: 0 }],
      { duration: GLOW_FADE_OUT_MS, easing: 'ease-out', fill: 'forwards' });
}

// ---------- Pictures: full height, real transparency ----------
// The picture is shown whole (never cropped) at its own proportions, as large
// as the free space allows, with only a faint see-through panel behind it, so
// transparent areas of a PNG stay mostly transparent. These styles are set from here
// so they win over whatever welcome.css says.
// The panel behind the picture: only visible through transparent areas.
// Lower the last number (0 to 1) for more see-through, 0 for none.
const PICTURE_BG = 'rgba(50, 52, 54, 0.22)';
// The thin border around the picture (and its rounded corners).
// Lower the last number (0 to 1) for fainter, 0 for none.
const PICTURE_BORDER = 'rgba(74, 77, 80, 0.45)';
const PICTURE_RADIUS = 6;      // px, 0 for square corners
const imgEl = $('slide-img');
const shotEl = imgEl.parentElement;
const frameEl = slideEl.parentElement;
function setImportant(el, props) {
  Object.keys(props).forEach((k) => el.style.setProperty(k, props[k], 'important'));
}
setImportant(shotEl, {
  'aspect-ratio': 'auto', width: 'auto', height: 'auto',
  'min-width': '0', 'max-width': '100%',
  background: PICTURE_BG, border: '2px solid ' + PICTURE_BORDER,
  'border-radius': PICTURE_RADIUS + 'px',
  overflow: 'visible', 'justify-self': 'center',
});
setImportant(imgEl, {
  display: 'block', width: 'auto', height: 'auto', 'max-width': '100%',
  'object-fit': 'contain', 'border-radius': Math.max(0, PICTURE_RADIUS - 2) + 'px',
});

// ---- Size and spacing (tune these) ----
const FRAME_MAX = 1400;           // px: widest the slide area may be on a big window
const TEXT_GAP = 40;              // px: space between the picture and the text
const TEXT_WIDTH_BELOW = '85ch';  // how wide the text is when it sits under the picture
const ALLOW_UPSCALE = true;       // enlarge smaller pictures to fill the free space
const MAX_UPSCALE = 2;          // ...but never more than this many times
const PICTURE_FILL = 0.97;        // use this share of the free space (1 = all of it)
setImportant(frameEl, { width: 'min(' + FRAME_MAX + 'px, 100%)' });
setImportant(slideEl, {
  'column-gap': TEXT_GAP + 'px',
  'row-gap': Math.round(TEXT_GAP * 0.6) + 'px',
});

// Make the picture as large as the free space allows (never cropped), keeping
// its proportions. With the text under the picture, the text height is taken
// off the available height; with the text beside it, the picture gets its
// whole column.
function fitPicture() {
  const layout = slideEl.dataset.layout;
  const copy = slideEl.querySelector('.copy');
  setImportant($('slide-body'), {
    'max-width': layout === 'below' ? TEXT_WIDTH_BELOW : 'none',
  });

  const natW = imgEl.naturalWidth;
  const natH = imgEl.naturalHeight;
  if (!natW || !natH) return;                      // not loaded yet: 'load' calls us again

  const cs = getComputedStyle(slideEl);
  let availH = frameEl.clientHeight;
  if (layout === 'below') {
    availH -= (copy ? copy.offsetHeight : 0) + (parseFloat(cs.rowGap) || 0);
  }
  // Width of the column the picture sits in.
  const tracks = cs.gridTemplateColumns.split(' ').map(parseFloat);
  let availW = layout === 'text-left' ? tracks[1] : tracks[0];
  if (!(availW > 0)) availW = slideEl.clientWidth * 0.6;

  availW -= 4;                                     // the picture's border
  availH -= 4 + 8;                                 // border + a little breathing room
  const size = (slides[index] && slides[index].size) || 1;
  let scale = Math.min(availW / natW, availH / natH) * PICTURE_FILL * size;
  scale = ALLOW_UPSCALE ? Math.min(scale, MAX_UPSCALE) : Math.min(scale, 1);
  scale = Math.max(scale, 0.05);
  setImportant(imgEl, {
    width: Math.floor(natW * scale) + 'px',
    height: Math.floor(natH * scale) + 'px',
  });
}
window.addEventListener('resize', fitPicture);
imgEl.addEventListener('load', fitPicture);

// Slide effect "frame": a faint rainbow glow that follows the picture's own
// shape (so it also works around a transparent picture), slowly changing colors.
const FRAME_COLORS = SPECTRUM;
// How each glow looks: distance, softness and strength (alpha: 00 to ff).
const GLOW_LOOKS = {
  frame: { offset: 8,  blur: 12, alpha: '50' },   // around the pictures
  logo:  { offset: 10, blur: 26, alpha: '99' },   // bigger and stronger, for the logo
};
function frameFilter(k, mode) {
  // Four neighbouring colors (one per side), so where they overlap at the
  // corners they mix into another color instead of turning white.
  const g = GLOW_LOOKS[mode] || GLOW_LOOKS.frame;
  const c = (i) => FRAME_COLORS[(k + i) % FRAME_COLORS.length] + g.alpha;
  const o = g.offset + 'px';
  const n = '-' + o;
  const r = g.blur + 'px ';
  return 'drop-shadow(0 ' + n + ' ' + r + c(0) + ') drop-shadow(' + o + ' 0 ' + r + c(1) +
         ') drop-shadow(0 ' + o + ' ' + r + c(2) + ') drop-shadow(' + n + ' 0 ' + r + c(3) + ')';
}
let frameGlowAnim = null;
let frameGlowMode = '';
function setFrameGlow(mode) {
  if (!mode) {
    if (frameGlowAnim) { frameGlowAnim.cancel(); frameGlowAnim = null; }
    frameGlowMode = '';
    imgEl.style.removeProperty('filter');
    return;
  }
  if (reduceMotion) { imgEl.style.setProperty('filter', frameFilter(0, mode)); return; }
  if (frameGlowAnim && frameGlowMode === mode) return;
  if (frameGlowAnim) frameGlowAnim.cancel();
  frameGlowMode = mode;
  const frames = [];
  for (let k = 0; k <= FRAME_COLORS.length; k++) frames.push({ filter: frameFilter(k, mode) });
  frameGlowAnim = imgEl.animate(frames, { duration: 14000, iterations: Infinity, easing: 'linear' });
}

// Text of a slide: the title (optional), the text and/or a bullet list.
function renderText(s) {
  const titleEl = $('slide-title');
  titleEl.textContent = s.title || '';
  titleEl.style.display = s.title ? '' : 'none';
  const bodyEl = $('slide-body');
  bodyEl.textContent = s.body || '';
  if (s.points) {
    const ul = document.createElement('ul');
    Object.assign(ul.style, {
      display: 'inline-block', textAlign: 'left', margin: '0', paddingLeft: '1.3em',
    });
    s.points.forEach((text) => {
      const li = document.createElement('li');
      li.style.margin = '0.4em 0';
      li.textContent = text;
      ul.appendChild(li);
    });
    bodyEl.appendChild(ul);
  }
}

function render() {
  const s = slides[index];
  const effect = s.effect || '';
  slideEl.dataset.layout = s.layout || 'below';
  document.body.dataset.effect = (effect === 'frame' || effect === 'logo') ? 'frame-img' : effect;
  if (effect === 'edge') { if (lastEffect !== 'edge') burstEdge(); } else fadeOutEdge();
  if (effect === 'bottom') showBottomGlow(); else hideBottomGlow();
  lastEffect = effect;
  $('slide-img').src = s.img;
  renderText(s);
  setImportant(shotEl, s.bare
      ? { background: 'transparent', border: '0' }
      : { background: PICTURE_BG, border: '2px solid ' + PICTURE_BORDER });
  setImportant(imgEl, { 'border-radius': (s.bare ? 0 : Math.max(0, PICTURE_RADIUS - 2)) + 'px' });
  setFrameGlow(effect === 'frame' || effect === 'logo' ? effect : '');
  setImportant(slideEl, {
    'column-gap': (s.gap != null ? s.gap : TEXT_GAP) + 'px',
    'row-gap': (s.gap != null ? s.gap : Math.round(TEXT_GAP * 0.6)) + 'px',
  });
  fitPicture();
  $('prev').disabled = index === 0;
  $('next').disabled = index === slides.length - 1;
  document.querySelectorAll('.dot').forEach((d, n) =>
      d.classList.toggle('on', n === index));
}

const OPPOSITE = { left: 'right', right: 'left', up: 'down', down: 'up' };
const DIRS = { left: [-1, 0], right: [1, 0], up: [0, -1], down: [0, 1] };
const OUT_MS = 380;                    // old slide leaves
const IN_MS = 700;                     // new slide arrives
const TRAVEL = 0.22;                   // how far it slides (fraction of the window)

// Load every picture once up front, so a slide never arrives with an empty frame.
slides.forEach((s) => { const img = new Image(); img.src = s.img; });

function goTo(n) {
  if (busy || n < 0 || n >= slides.length || n === index) return;
  busy = true;
  const fromEffect = slides[index].effect;
  const toEffect = slides[n].effect;
  if (fromEffect === 'bottom' && toEffect !== 'bottom') hideBottomGlow();
  if (fromEffect === 'edge' && toEffect !== 'edge') fadeOutEdge();
  // Forward: use the move of the slide we arrive on. Backward: undo the move
  // of the slide we are leaving.
  const forward = n > index;
  const raw = forward ? slides[n].move : OPPOSITE[slides[index].move];
  const move = raw || 'fade';
  const dir = DIRS[move] || [0, 0];
  const dx = dir[0] * window.innerWidth * TRAVEL;
  const dy = dir[1] * window.innerHeight * TRAVEL;
  nudgeStars(move);

  // The animations are driven from here (Web Animations), so they do not
  // depend on any CSS rule.
  const out = slideEl.animate(
      [{ opacity: 1, transform: 'translate(0px, 0px)' },
       { opacity: 0, transform: 'translate(' + dx + 'px,' + dy + 'px)' }],
      { duration: OUT_MS, easing: 'cubic-bezier(0.5, 0, 0.9, 0.6)', fill: 'forwards' });
  out.onfinish = () => {
    index = n;
    render();
    out.cancel();
    const arrive = slideEl.animate(
        [{ opacity: 0, transform: 'translate(' + (-dx) + 'px,' + (-dy) + 'px)' },
         { opacity: 1, transform: 'translate(0px, 0px)' }],
        { duration: IN_MS, easing: 'cubic-bezier(0.2, 0.8, 0.2, 1)', fill: 'both' });
    arrive.onfinish = () => { arrive.cancel(); busy = false; };
  };
}

function buildDots() {
  const dots = $('dots');
  slides.forEach((_, n) => {
    const b = document.createElement('button');
    b.className = 'dot';
    b.setAttribute('aria-label', 'Go to slide ' + (n + 1));
    b.addEventListener('click', () => goTo(n));
    dots.appendChild(b);
  });
}

$('prev').addEventListener('click', () => goTo(index - 1));
$('next').addEventListener('click', () => goTo(index + 1));
$('start').addEventListener('click', startOutro);
document.addEventListener('keydown', (e) => {
  if (outroActive || document.body.classList.contains('intro')) return;
  if (e.key === 'ArrowLeft') goTo(index - 1);
  if (e.key === 'ArrowRight') goTo(index + 1);
});

// ---------- Space intro ----------
// Space fades in with a very slow drift, the stars speed up into a warp while
// the DOCKIUM title shows, then they slow down again. Slide 1 fades in on top
// and the stars keep drifting slowly underneath it.
const INTRO_MS = 5400;
const BASE_SPEED = 0.0015;      // the slow drift (start, and under the slides)
const PEAK_SPEED = 0.05;        // top warp speed
const PAN_PEAK = 0.010;         // top speed of the push when a slide moves
const PAN_MS = 1500;            // how long the push lasts: slow -> fast -> slow
// Outro (Start Dockium): slide zooms out -> stars accelerate -> space fades
// away into the new tab page's background color -> the new tab page opens.
const OUTRO_SLIDE_MS = 650;     // the slide zooms out and fades, controls fade
const OUTRO_ACCEL_AT = 400;     // the stars start to accelerate (ms after Start)
const OUTRO_ACCEL_MS = 1500;    // ...and keep accelerating for this long
const OUTRO_PEAK = 0.30;        // top star speed at the end of the outro
const OUTRO_FADE_AT = 1600;     // the space starts fading away
const OUTRO_FADE_MS = 1000;
const OUTRO_END = OUTRO_FADE_AT + OUTRO_FADE_MS;   // then the new tab opens
const OUTRO_END_COLOR = '#363840';  // the new tab page's background: the space fades into this
const smooth = (x) => { x = Math.min(1, Math.max(0, x)); return x * x * (3 - 2 * x); };
const spaceEl = $('space');
const canvas = $('stars');
const ctx = canvas.getContext('2d');
let introRunning = true;
let introTimer = 0;
let rafId = 0;
let curSpeed = BASE_SPEED;
let panX = 0;                   // current push this frame, in star-field units
let panY = 0;
let panActive = false;          // a push is running
let panStart = 0;               // when it started (ms)
let panDirX = 0;
let panDirY = 0;
let lastFrame = 0;
let outroActive = false;
let outroStart = 0;

const stars = [];
const STAR_COUNT = 520;
let W = 0, H = 0;

function resizeSpace() {
  const dpr = window.devicePixelRatio || 1;
  W = window.innerWidth;
  H = window.innerHeight;
  canvas.width = W * dpr;
  canvas.height = H * dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function resetStar(s, anywhere) {
  s.x = (Math.random() * 2 - 1);
  s.y = (Math.random() * 2 - 1);
  s.z = anywhere ? Math.random() : 1;
  s.hue = Math.random() < 0.2 ? 210 + Math.random() * 60 : 40 + Math.random() * 20;
  s.sat = Math.random() < 0.2 ? 80 : 20;
  return s;
}

// A slide is moving: push the stars the same way. The push starts slow,
// speeds up, then slows down again (nearer stars move more).
function nudgeStars(move) {
  const dir = DIRS[move];
  if (!dir) return;
  panDirX = dir[0];
  panDirY = dir[1];
  panStart = performance.now();
  panActive = true;
}

// While the stars are being pushed, a star that leaves the screen comes back
// in just past the opposite edge instead of in the middle.
function respawnIncoming(s, half) {
  s.z = 0.15 + Math.random() * 0.85;
  const halfW = ((W / 2 + 30) / half) * s.z;
  const halfH = ((H / 2 + 30) / half) * s.z;
  if (Math.abs(panX) >= Math.abs(panY)) {
    s.x = (panX < 0 ? 1 : -1) * halfW;
    s.y = (Math.random() * 2 - 1) * halfH;
  } else {
    s.y = (panY < 0 ? 1 : -1) * halfH;
    s.x = (Math.random() * 2 - 1) * halfW;
  }
}

function startIntro() {
  resizeSpace();
  window.addEventListener('resize', resizeSpace);
  for (let i = 0; i < STAR_COUNT; i++) stars.push(resetStar({}, true));
  const t0 = performance.now();

  function frame(now) {
    const t = (now - t0) / 1000;
    // slow start -> speed up to a warp -> ease back down to a slow drift
    let k = 0;
    if (introRunning) {
      k = t < 3.2 ? Math.pow(smooth(t / 3.2), 2) : Math.exp(-(t - 3.2) / 1.1);
    }
    const target = BASE_SPEED + PEAK_SPEED * k;
    if (outroActive) {
      // accelerate harder and harder
      const u = Math.min(1, Math.max(0,
          (performance.now() - outroStart - OUTRO_ACCEL_AT) / OUTRO_ACCEL_MS));
      curSpeed = BASE_SPEED + OUTRO_PEAK * u * u;
    } else {
      curSpeed += (target - curSpeed) * 0.08;    // smooth, also after a skip
    }
    const speed = curSpeed;
    const dt = lastFrame ? Math.min(50, now - lastFrame) : 16.7;
    lastFrame = now;
    panX = 0;
    panY = 0;
    if (panActive) {
      const u = (performance.now() - panStart) / PAN_MS;
      if (u >= 1) {
        panActive = false;
      } else {
        const ease = Math.pow(Math.sin(Math.PI * u), 2);   // 0 -> 1 -> 0
        const step = PAN_PEAK * ease * (dt / 16.7);
        panX = panDirX * step;
        panY = panDirY * step;
      }
    }
    const panning = panActive && Math.abs(panX) + Math.abs(panY) > 0.0005;
    ctx.clearRect(0, 0, W, H);
    const cx = W / 2, cy = H / 2, scale = Math.max(W, H) * 0.5;
    const half = scale * 0.5;
    for (const s of stars) {
      const pz = s.z + speed * 3.2;                 // earlier position -> streak
      s.z -= speed;
      s.x += panX;
      s.y += panY;
      if (s.z <= 0.02) { resetStar(s, false); continue; }
      const x2 = cx + (s.x / s.z) * half;
      const y2 = cy + (s.y / s.z) * half;
      const x1 = cx + ((s.x - panX * 3.2) / pz) * half;
      const y1 = cy + ((s.y - panY * 3.2) / pz) * half;
      if (x2 < -50 || x2 > W + 50 || y2 < -50 || y2 > H + 50) {
        if (panning) respawnIncoming(s, half); else resetStar(s, false);
        continue;
      }
      const a = Math.min(1, (1 - s.z) * 1.6 + 0.1);
      ctx.strokeStyle = 'hsla(' + s.hue + ',' + s.sat + '%,92%,' + a + ')';
      ctx.lineWidth = Math.max(0.6, (1 - s.z) * 2.2);
      ctx.lineCap = 'round';
      ctx.beginPath();
      ctx.moveTo(x1, y1);
      ctx.lineTo(x2, y2);
      ctx.stroke();
    }
    rafId = requestAnimationFrame(frame);
  }
  rafId = requestAnimationFrame(frame);
  introTimer = setTimeout(endIntro, INTRO_MS);
}

function endIntro() {
  if (!introRunning) return;
  introRunning = false;
  clearTimeout(introTimer);
  spaceEl.classList.add('behind');                    // stars move under the slides
  document.body.classList.remove('intro');            // slide 1 + controls fade in
  requestAnimationFrame(fitPicture);
}

// ---------- Outro: Start Dockium ----------
// Covers the whole window with the end color, fading it in.
function fadeToEndColor(ms) {
  const cover = document.createElement('div');
  Object.assign(cover.style, {
    position: 'fixed', top: '0', left: '0', right: '0', bottom: '0',
    background: OUTRO_END_COLOR, opacity: '0', pointerEvents: 'none',
    zIndex: '2147483647',
  });
  document.body.appendChild(cover);
  cover.animate([{ opacity: 0 }, { opacity: 1 }],
      { duration: ms, easing: 'ease-in-out', fill: 'forwards' });
}

function startOutro() {
  if (outroActive || busy || document.body.classList.contains('intro')) return;
  outroActive = true;
  busy = true;                                     // blocks arrows and dots
  outroStart = performance.now();
  document.body.style.pointerEvents = 'none';
  wantMusic = false;
  if (!music.paused) fadeTo(0, OUTRO_END);         // the music fades out too

  const go = () => { location.href = 'chrome://newtab'; };
  if (reduceMotion) { go(); return; }

  // 1. The slide zooms out and fades; the controls and glows fade.
  slideEl.animate(
      [{ opacity: 1, transform: 'scale(1)' }, { opacity: 0, transform: 'scale(0.78)' }],
      { duration: OUTRO_SLIDE_MS, easing: 'ease-in', fill: 'forwards' });
  [$('prev'), $('next'), $('dots'), $('start'), $('mute'), edgeEl, glowBottom]
      .forEach((el) => el.animate([{ opacity: 0 }],
          { duration: OUTRO_SLIDE_MS * 0.8, easing: 'ease-in', fill: 'forwards' }));

  if (!document.body.contains(spaceEl)) {          // no stars: just open the tab
    fadeToEndColor(OUTRO_SLIDE_MS);
    setTimeout(go, OUTRO_SLIDE_MS + 50);
    return;
  }

  // 2. The stars accelerate (see frame()).
  // 3. The whole space fades into the new tab page's background color.
  setTimeout(() => fadeToEndColor(OUTRO_FADE_MS), OUTRO_FADE_AT);

  // 4. Open the new tab page.
  setTimeout(go, OUTRO_END + 100);
}

// ---------- Background music (intro.mp3) ----------
// Browsers often block audio until the user clicks or presses a key, so we
// try to start right away and, if that is blocked, start on the first
// interaction. The speaker button in the corner mutes / unmutes.
const music = $('music');
const muteBtn = $('mute');
const TARGET_VOLUME = 0.5;
let wantMusic = true;      // becomes false when the user mutes
let fadeTimer = 0;
let resumeOnShow = false;

function fadeTo(volume, ms, done) {
  clearInterval(fadeTimer);
  const from = music.volume;
  const steps = Math.max(1, Math.round(ms / 50));
  let n = 0;
  fadeTimer = setInterval(() => {
    n++;
    music.volume = Math.min(1, Math.max(0, from + (volume - from) * n / steps));
    if (n >= steps) {
      clearInterval(fadeTimer);
      if (done) done();
    }
  }, 50);
}

function updateMuteButton() {
  const playing = !music.paused && wantMusic;
  muteBtn.classList.toggle('off', !playing);
  muteBtn.setAttribute('aria-pressed', String(!playing));
  muteBtn.setAttribute('aria-label', playing ? 'Mute music' : 'Play music');
}

function onFirstGesture(e) {
  if (e.target.closest && e.target.closest('#mute')) return;  // the button decides itself
  startPlayback();
}

async function startPlayback() {
  music.volume = 0;
  try {
    await music.play();
    fadeTo(TARGET_VOLUME, 2000);
    document.removeEventListener('pointerdown', onFirstGesture);
    document.removeEventListener('keydown', onFirstGesture);
  } catch (e) {
    // Blocked for now: wait for the first click or key press.
  }
  updateMuteButton();
}

function setMusic(on) {
  wantMusic = on;
  if (on) {
    if (music.paused) startPlayback(); else fadeTo(TARGET_VOLUME, 500);
  } else {
    fadeTo(0, 300, () => music.pause());
  }
  updateMuteButton();
}

muteBtn.addEventListener('click', () => setMusic(music.paused || !wantMusic));
music.addEventListener('error', () => { muteBtn.hidden = true; });   // no intro.mp3: no button
document.addEventListener('pointerdown', onFirstGesture);
document.addEventListener('keydown', onFirstGesture);
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    resumeOnShow = !music.paused;
    music.pause();
  } else if (resumeOnShow && wantMusic) {
    music.play().catch(() => {});
  }
});
// If the page asked too early (file not ready yet), try again once it is.
music.addEventListener('canplay', () => {
  if (wantMusic && music.paused) startPlayback();
}, { once: true });
startPlayback();

buildDots();
render();

// The intro cannot be skipped: it plays out, and endIntro() runs from its timer.

if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
  document.body.classList.remove('intro');
  spaceEl.remove();
  introRunning = false;
} else {
  startIntro();
}
