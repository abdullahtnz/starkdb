/* ============================================================
   STARKDB — Main JavaScript
   Mobile menu · Tabs · Copy · Sidebar · Scroll spy
   ============================================================ */
(function () {
  'use strict';

  /* ---- DOM READY ---- */
  document.addEventListener('DOMContentLoaded', init);

  function init() {
    initMobileNav();
    initTabs();
    initCopyButtons();
    initSidebar();
    initScrollSpy();
    initSmoothAnchors();
  }

  /* ============================================================
     MOBILE NAV
     ============================================================ */
  function initMobileNav() {
    var hamburger = document.getElementById('hamburger');
    var mobileNav = document.getElementById('mobileNav');
    if (!hamburger || !mobileNav) return;

    function open() {
      mobileNav.classList.add('open');
      hamburger.classList.add('open');
      hamburger.setAttribute('aria-expanded', 'true');
      document.body.style.overflow = 'hidden';
    }
    function close() {
      mobileNav.classList.remove('open');
      hamburger.classList.remove('open');
      hamburger.setAttribute('aria-expanded', 'false');
      document.body.style.overflow = '';
    }
    function toggle() {
      mobileNav.classList.contains('open') ? close() : open();
    }

    hamburger.addEventListener('click', toggle);

    // Close when clicking a link inside mobile nav
    mobileNav.querySelectorAll('a').forEach(function (link) {
      link.addEventListener('click', close);
    });

    // Close on escape
    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape' && mobileNav.classList.contains('open')) close();
    });
  }

  /* ============================================================
     TAB SWITCHING
     ============================================================ */
  function initTabs() {
    // Delegated: works on any page
    document.addEventListener('click', function (e) {
      var btn = e.target.closest('.tab-btn');
      if (!btn) return;

      var tabsContainer = btn.closest('.tabs');
      var tabId = btn.getAttribute('aria-controls');
      if (!tabId) {
        // Fallback: try to infer from text
        var idx = Array.from(tabsContainer.querySelectorAll('.tab-btn')).indexOf(btn);
        var contents = tabsContainer.parentElement.querySelectorAll('.tab-content');
        if (contents[idx]) tabId = contents[idx].id;
      }
      if (!tabId) return;

      // Deactivate all buttons in this group
      tabsContainer.querySelectorAll('.tab-btn').forEach(function (b) {
        b.classList.remove('active');
        b.setAttribute('aria-selected', 'false');
      });
      btn.classList.add('active');
      btn.setAttribute('aria-selected', 'true');

      // Show correct content
      var parent = tabsContainer.parentElement;
      parent.querySelectorAll('.tab-content').forEach(function (c) {
        c.classList.remove('active');
      });
      var target = document.getElementById(tabId);
      if (target) target.classList.add('active');
    });
  }

  /* ============================================================
     COPY CODE BUTTONS
     ============================================================ */
  function initCopyButtons() {
    document.addEventListener('click', function (e) {
      var btn = e.target.closest('.copy-btn');
      if (!btn) return;

      var codeEl = btn.nextElementSibling;
      if (!codeEl) return;

      var text = codeEl.innerText;
      navigator.clipboard.writeText(text).then(function () {
        btn.textContent = 'COPIED!';
        btn.classList.add('copied');
        setTimeout(function () {
          btn.textContent = 'COPY';
          btn.classList.remove('copied');
        }, 2000);
      }).catch(function () {
        // Fallback for older browsers
        var ta = document.createElement('textarea');
        ta.value = text;
        ta.style.position = 'fixed';
        ta.style.opacity = '0';
        document.body.appendChild(ta);
        ta.select();
        document.execCommand('copy');
        document.body.removeChild(ta);
        btn.textContent = 'COPIED!';
        btn.classList.add('copied');
        setTimeout(function () {
          btn.textContent = 'COPY';
          btn.classList.remove('copied');
        }, 2000);
      });
    });
  }

  /* ============================================================
     SIDEBAR (DOCS PAGE)
     ============================================================ */
  function initSidebar() {
    var sidebar = document.getElementById('sidebar');
    var toggle = document.getElementById('sidebarToggle');
    var overlay = document.getElementById('sidebarOverlay');
    if (!sidebar || !toggle) return;

    function openSidebar() {
      sidebar.classList.add('open');
      if (overlay) overlay.classList.add('open');
    }
    function closeSidebar() {
      sidebar.classList.remove('open');
      if (overlay) overlay.classList.remove('open');
    }

    toggle.addEventListener('click', function () {
      sidebar.classList.contains('open') ? closeSidebar() : openSidebar();
    });

    if (overlay) {
      overlay.addEventListener('click', closeSidebar);
    }

    // Close sidebar when clicking a sidebar link (mobile)
    sidebar.querySelectorAll('a').forEach(function (link) {
      link.addEventListener('click', function () {
        if (window.innerWidth <= 900) closeSidebar();
      });
    });
  }

  /* ============================================================
     SCROLL SPY — highlight active sidebar link
     ============================================================ */
  function initScrollSpy() {
    var sidebarLinks = document.querySelectorAll('.sidebar-links a[href^="#"]');
    if (!sidebarLinks.length) return;

    var sectionMap = [];
    sidebarLinks.forEach(function (link) {
      var id = link.getAttribute('href').slice(1);
      var el = document.getElementById(id);
      if (el) sectionMap.push({ el: el, link: link });
    });

    if (!sectionMap.length) return;

    var ticking = false;
    window.addEventListener('scroll', function () {
      if (ticking) return;
      ticking = true;
      requestAnimationFrame(function () {
        var scrollY = window.scrollY + 120;
        var current = sectionMap[0];
        for (var i = 0; i < sectionMap.length; i++) {
          if (sectionMap[i].el.offsetTop <= scrollY) {
            current = sectionMap[i];
          }
        }
        sidebarLinks.forEach(function (l) { l.classList.remove('active'); });
        if (current) current.link.classList.add('active');
        ticking = false;
      });
    });
  }

  /* ============================================================
     SMOOTH SCROLL FOR ANCHOR LINKS
     ============================================================ */
  function initSmoothAnchors() {
    document.addEventListener('click', function (e) {
      var link = e.target.closest('a[href^="#"]');
      if (!link) return;
      var id = link.getAttribute('href').slice(1);
      if (!id) return;
      var target = document.getElementById(id);
      if (!target) return;

      e.preventDefault();
      var offset = 80; // navbar height
      var top = target.getBoundingClientRect().top + window.scrollY - offset;
      window.scrollTo({ top: top, behavior: 'smooth' });

      // Update URL hash without jumping
      history.pushState(null, '', '#' + id);
    });
  }

})();
