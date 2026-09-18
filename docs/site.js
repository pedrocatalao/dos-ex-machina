/* DOS ex Machina - the site's few moving parts.  No framework, no build:
 * the pages read complete without any of this, and each part below only
 * adds to what is already there. */
(function () {
  "use strict";
  var REPO = "pedrocatalao/dos-ex-machina";
  var root = document.documentElement;
  var $ = function (s, el) { return (el || document).querySelector(s); };
  var $$ = function (s, el) { return Array.prototype.slice.call((el || document).querySelectorAll(s)); };

  /* ---- the room's lights ------------------------------------------------
   * No stored choice means the visitor's system decides; the rocker stores
   * one.  The attribute is set before first paint by the inline snippet in
   * each page's head, so this only handles the switch itself. */
  function lightsAreOff() {
    var t = root.getAttribute("data-theme");
    if (t) return t === "dark";
    return window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches;
  }
  $$(".lights").forEach(function (btn) {
    function say() { btn.setAttribute("aria-pressed", lightsAreOff() ? "true" : "false"); }
    say();
    btn.addEventListener("click", function () {
      var next = lightsAreOff() ? "light" : "dark";
      root.setAttribute("data-theme", next);
      try { localStorage.setItem("dxm-lights", next); } catch (e) { /* private mode: not kept */ }
      say();
    });
  });

  /* ---- the seven-segment display ------------------------------------------
   * The turbo display's own geometry (src/segdisp.h): a digit 0.58 as wide
   * as it is tall, segments 0.15 of its height thick with pointed ends
   * standing a hairline apart, leaning 0.10 like every LED digit of the
   * period.  Unlit segments show faintly through the glass, as they do on
   * the case. */
  var GLYPH = {
    "0": "abcdef", "1": "bc", "2": "abdeg", "3": "abcdg", "4": "bcfg", "5": "acdfg",
    "6": "acdefg", "7": "abc", "8": "abcdefg", "9": "abcdfg", "-": "g", " ": "",
    "L": "def", "A": "abcefg", "T": "defg", "E": "adefg", "S": "acdfg",
    "B": "cdefg", "R": "eg", "C": "adef", "D": "bcdeg", "N": "ceg", "O": "cdeg", "P": "abefg", "U": "bcdef", "H": "bcefg", "I": "c"
  };
  var H = 100, W = 58, T = 15, HT = T / 2, G = 2.4;
  var L = HT, R = W - HT, TOP = HT, MID = H / 2, BOT = H - HT;
  function hbar(x0, x1, y) {
    return [[x0 + G, y], [x0 + G + HT, y - HT], [x1 - G - HT, y - HT], [x1 - G, y], [x1 - G - HT, y + HT], [x0 + G + HT, y + HT]];
  }
  function vbar(x, y0, y1) {
    return [[x, y0 + G], [x + HT, y0 + G + HT], [x + HT, y1 - G - HT], [x, y1 - G], [x - HT, y1 - G - HT], [x - HT, y0 + G + HT]];
  }
  var SEGS = { a: hbar(L, R, TOP), g: hbar(L, R, MID), d: hbar(L, R, BOT), f: vbar(L, TOP, MID), b: vbar(R, TOP, MID), e: vbar(L, MID, BOT), c: vbar(R, MID, BOT) };
  function digit(ch, dot) {
    var on = GLYPH[ch] === undefined ? "" : GLYPH[ch], out = "";
    Object.keys(SEGS).forEach(function (k) {
      var pts = SEGS[k].map(function (p) { return p[0].toFixed(1) + "," + p[1].toFixed(1); }).join(" ");
      out += '<polygon class="' + (on.indexOf(k) >= 0 ? "s-on" : "s-off") + '" points="' + pts + '"/>';
    });
    out += '<circle class="' + (dot ? "s-on" : "s-off") + '" cx="' + (W + 9) + '" cy="' + BOT + '" r="5.6"/>';
    return '<svg viewBox="-2 0 84 100" aria-hidden="true" focusable="false"><g transform="translate(11 0) skewX(-5.71)">' + out + "</g></svg>";
  }
  function sevenSeg(el, text) {
    var s = String(text).toUpperCase(), cells = [];
    for (var i = 0; i < s.length; i++) {
      if (s[i] === "." && cells.length) { cells[cells.length - 1].dot = true; continue; }
      cells.push({ ch: s[i], dot: false });
    }
    el.innerHTML = cells.map(function (c) { return digit(c.ch, c.dot); }).join("");
    el.setAttribute("role", "img");
    el.setAttribute("aria-label", "Version: " + text);
  }

  /* ---- what is on the tube ------------------------------------------------ */
  var frames = $(".frames");
  if (frames) {
    var caps = $$(".channels .cap"), caption = $(".caption"), touched = false, at = 0, timer = null;
    var show = function (i) {
      at = i;
      caps.forEach(function (c, n) { c.setAttribute("aria-pressed", n === i ? "true" : "false"); });
      var want = caps[i].getAttribute("data-frame");
      $$("img", frames).forEach(function (im) {
        var mine = im.getAttribute("data-frame") === want;
        if (mine && im.hasAttribute("data-src")) {           /* fetched the first time it is wanted */
          im.src = im.getAttribute("data-src");
          if (im.hasAttribute("data-srcset")) im.srcset = im.getAttribute("data-srcset");
          im.removeAttribute("data-src");
        }
        im.classList.toggle("on", mine);
      });
      if (caption) caption.innerHTML = caps[i].getAttribute("data-caption");
    };
    caps.forEach(function (c, i) {
      c.addEventListener("click", function () { touched = true; if (timer) clearInterval(timer); show(i); });
    });
    show(0);
    var still = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    if (!still) {
      timer = setInterval(function () { if (!touched && !document.hidden) show((at + 1) % caps.length); }, 7000);
    }
  }

  /* ---- Figure 1: the pins and the legend point at each other -------------- */
  $$(".pin").forEach(function (pin) {
    var li = $('.legend li[data-pin="' + pin.getAttribute("data-pin") + '"]');
    if (!li) return;
    var on = function () { pin.classList.add("hot"); li.classList.add("hot"); };
    var off = function () { pin.classList.remove("hot"); li.classList.remove("hot"); };
    [pin, li].forEach(function (el) {
      el.addEventListener("mouseenter", on); el.addEventListener("mouseleave", off);
      el.addEventListener("focusin", on); el.addEventListener("focusout", off);
    });
    pin.addEventListener("click", function () { li.scrollIntoView({ block: "nearest", behavior: "smooth" }); });
  });

  /* ---- copy a command ------------------------------------------------------ */
  $$(".copy").forEach(function (b) {
    b.addEventListener("click", function () {
      var pre = $("pre", b.parentNode);
      var text = $$(".ln", pre).map(function (l) { return l.textContent; }).join("\n") || pre.textContent;
      var done = function () { b.textContent = "Copied"; setTimeout(function () { b.textContent = "Copy"; }, 1600); };
      if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(text).then(done, function () {});
    });
  });

  /* ---- the release, asked of GitHub -----------------------------------------
   * The newest published release, pre-releases included, since that is what
   * the first ones will be.  With one: its number on the display and a
   * download for the visitor's system.  Without one - today - the display
   * reads LATEST, the lamp says SOURCE, and the way in is to build it; the
   * page changes over by itself the day a release is published.  The answer
   * is kept for ten minutes, GitHub's unauthenticated limit being what it
   * is. */
  var segEl = $("#ver-seg");
  if (segEl) {
    var ASSETS = [
      { os: "mac", name: "dxm-macos-universal.zip", label: "macOS", detail: "Universal: Apple Silicon and Intel" },
      { os: "win", name: "dxm-windows-x86_64.zip", label: "Windows", detail: "x86_64" },
      { os: "win-arm", name: "dxm-windows-arm64.zip", label: "Windows on ARM", detail: "arm64" },
      { os: "linux", name: "dxm-linux-x86_64.tar.gz", label: "Linux", detail: "x86_64 tarball" },
      { os: "linux-arm", name: "dxm-linux-arm64.tar.gz", label: "Linux on ARM", detail: "arm64 tarball" }
    ];
    var here = (function () {
      var p = ((navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || "").toLowerCase();
      var ua = (navigator.userAgent || "").toLowerCase();
      var arm = /aarch64|arm64/.test(ua);
      if (/mac|iphone|ipad/.test(p) || /mac os/.test(ua)) return "mac";
      if (/win/.test(p)) return arm ? "win-arm" : "win";
      if (/linux|x11/.test(p) || /linux/.test(ua)) return arm ? "linux-arm" : "linux";
      return "";
    })();
    var lamp = function (which) { $$("#ver-lamps span").forEach(function (s) { s.classList.toggle("lit", s.getAttribute("data-lamp") === which); }); };
    var note = $("#ver-note"), cta = $("#cta-primary"), panel = $("#dl-panel"), state = $("#dl-state");

    var noRelease = function () {
      sevenSeg(segEl, "LATEST");
      lamp("src");
      if (note) note.innerHTML = "No release has been published yet. For now the way in is to build it, which takes a few minutes.";
      /* the static page already says all this; nothing else to change */
    };
    var withRelease = function (rel) {
      var tag = String(rel.tag_name || "").replace(/^v/i, "");
      var num = (tag.match(/^[0-9]+(?:\.[0-9]+)*/) || [""])[0], rest = tag.slice(num.length).replace(/^[-.]/, "");
      sevenSeg(segEl, num || "LATEST");
      lamp("rel");
      var found = {};
      (rel.assets || []).forEach(function (a) { found[a.name] = a.browser_download_url; });
      var mine = ASSETS.filter(function (a) { return a.os === here && found[a.name]; })[0];
      var chip = rel.prerelease ? ' <span class="chip">pre-release</span>' : (rest ? ' <span class="chip">' + rest + "</span>" : "");
      if (note) note.innerHTML = "Version " + tag + chip + ", published " + new Date(rel.published_at).toLocaleDateString(undefined, { year: "numeric", month: "long", day: "numeric" }) + ".";
      if (cta && mine) { cta.href = found[mine.name]; cta.innerHTML = "Download for " + mine.label + " <small>" + tag + "</small>"; }
      else if (cta) { cta.href = "#get"; cta.innerHTML = "Download <small>" + tag + "</small>"; }
      if (state) state.textContent = "Version " + tag + " is out. Each download is self-contained: unpack it and run it.";
      if (panel) {
        var rows = ASSETS.filter(function (a) { return found[a.name]; }).map(function (a) {
          return '<li><a href="' + found[a.name] + '">' + a.label + "</a><span>" + a.detail + "</span></li>";
        }).join("");
        if (rows) panel.innerHTML = '<ul class="dl-list">' + rows + '</ul><p class="figcap" style="margin-top:1rem"><a href="' + rel.html_url + '">Release notes and checksums on GitHub</a></p>';
      }
    };

    sevenSeg(segEl, "LATEST"); /* until the answer is in */
    var cached = null;
    try { cached = JSON.parse(sessionStorage.getItem("dxm-release") || "null"); } catch (e) { cached = null; }
    var use = function (rel) { if (rel && rel.tag_name) withRelease(rel); else noRelease(); };
    if (cached && Date.now() - cached.at < 600000) use(cached.rel);
    else if (window.fetch) {
      fetch("https://api.github.com/repos/" + REPO + "/releases?per_page=10", { headers: { Accept: "application/vnd.github+json" } })
        .then(function (r) { return r.ok ? r.json() : []; })
        .then(function (list) {
          var rel = (Array.isArray(list) ? list : []).filter(function (r) { return !r.draft; })[0] || null;
          try { sessionStorage.setItem("dxm-release", JSON.stringify({ at: Date.now(), rel: rel })); } catch (e) { /* not kept */ }
          use(rel);
        })
        .catch(noRelease);
    } else noRelease();
  }

  /* ---- the User's Guide: which chapter is being read ------------------------ */
  var toc = $$(".toc a");
  if (toc.length && "IntersectionObserver" in window) {
    var byId = {};
    toc.forEach(function (a) { byId[a.getAttribute("href").slice(1)] = a; });
    var seen = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        if (!e.isIntersecting) return;
        toc.forEach(function (a) { a.classList.remove("here"); });
        if (byId[e.target.id]) byId[e.target.id].classList.add("here");
      });
    }, { rootMargin: "-20% 0px -70% 0px" });
    $$(".chapter").forEach(function (c) { seen.observe(c); });
  }
})();
