/**
 * @file
 * The Moby Carousel: photos and videos shown one at a time.
 *
 * The slides sit side by side in a container that scrolls sideways and snaps
 * to each slide, so swiping is the browser's own, and the slides can still be
 * scrolled through without this script. The script adds the buttons and dots,
 * announces the slide shown to screen readers, keeps slides that are out of
 * view from being focused, and pauses videos that are scrolled away.
 *
 * It follows the WAI-ARIA Authoring Practices carousel pattern. Slides never
 * change on their own.
 */
(function (Backdrop) {
  'use strict';

  Backdrop.behaviors.mobyCarousel = {
    attach: function (context) {
      var root = context.querySelectorAll ? context : document;
      var carousels = root.querySelectorAll('.moby-carousel:not(.moby-carousel--single):not(.moby-carousel-processed)');
      Array.prototype.forEach.call(carousels, function (element) {
        element.classList.add('moby-carousel-processed');
        new MobyCarousel(element);
      });
    }
  };

  /**
   * Sets up one carousel.
   *
   * @param {HTMLElement} element
   *   The carousel's wrapper.
   */
  function MobyCarousel(element) {
    var self = this;
    this.element = element;
    this.viewport = element.querySelector('.moby-carousel-viewport');
    this.slides = element.querySelectorAll('.moby-carousel-slide');
    this.dots = element.querySelectorAll('.moby-carousel-dot');
    this.counter = element.querySelector('.moby-carousel-counter');
    this.status = element.querySelector('.moby-carousel-status');
    this.current = 0;
    // The slide being scrolled to by a button, while it scrolls there.
    this.target = null;
    this.settleTimer = null;
    this.reducedMotion = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)');

    element.classList.add('moby-carousel--enhanced');
    // The buttons replace scrolling with the keyboard.
    this.viewport.removeAttribute('tabindex');
    Array.prototype.forEach.call(element.querySelectorAll('.moby-carousel-buttons, .moby-carousel-dots, .moby-carousel-counter'), function (control) {
      control.hidden = false;
    });

    element.querySelector('.moby-carousel-previous').addEventListener('click', function () {
      self.go(self.current - 1);
    });
    element.querySelector('.moby-carousel-next').addEventListener('click', function () {
      self.go(self.current + 1);
    });
    Array.prototype.forEach.call(this.dots, function (dot, index) {
      dot.addEventListener('click', function () {
        self.go(index);
      });
    });

    this.viewport.addEventListener('scroll', function () {
      self.onScroll();
    }, { passive: true });

    // Keep the same slide in view when the carousel changes size.
    if (window.ResizeObserver) {
      new ResizeObserver(function () {
        self.scrollTo(self.current, false);
      }).observe(this.viewport);
    }

    this.show(0, false);
  }

  /**
   * Moves to a slide, wrapping around at either end.
   *
   * @param {number} index
   *   The slide's index. One past either end wraps to the other end.
   */
  MobyCarousel.prototype.go = function (index) {
    var count = this.slides.length;
    index = (index + count) % count;
    this.target = index;
    this.show(index, true);
    this.scrollTo(index, true);
  };

  /**
   * Scrolls the slides to one of them.
   *
   * @param {number} index
   *   The slide's index.
   * @param {boolean} animate
   *   Whether to scroll smoothly, unless the user prefers reduced motion.
   */
  MobyCarousel.prototype.scrollTo = function (index, animate) {
    var rtl = window.getComputedStyle(this.viewport).direction === 'rtl';
    var smooth = animate && !(this.reducedMotion && this.reducedMotion.matches);
    this.viewport.scrollTo({
      left: index * this.viewport.clientWidth * (rtl ? -1 : 1),
      behavior: smooth ? 'smooth' : 'auto'
    });
  };

  /**
   * Responds to the slides being scrolled, by swiping or by a button.
   */
  MobyCarousel.prototype.onScroll = function () {
    var self = this;
    // Wait for scrolling to stop, so slides passed on the way aren't
    // announced.
    window.clearTimeout(this.settleTimer);
    this.settleTimer = window.setTimeout(function () {
      var width = self.viewport.clientWidth || 1;
      var index = Math.round(Math.abs(self.viewport.scrollLeft) / width);
      index = Math.max(0, Math.min(self.slides.length - 1, index));
      var target = self.target;
      self.target = null;
      if (index === target) {
        // A button already showed this slide.
        return;
      }
      if (index !== self.current) {
        self.show(index, true);
      }
    }, 150);
  };

  /**
   * Makes a slide the current one.
   *
   * @param {number} index
   *   The slide's index.
   * @param {boolean} announce
   *   Whether to tell screen reader users which slide is shown.
   */
  MobyCarousel.prototype.show = function (index, announce) {
    var count = this.slides.length;
    this.current = index;
    Array.prototype.forEach.call(this.slides, function (slide, slideIndex) {
      var current = slideIndex === index;
      // Slides out of view can't be focused or read, and stop playing.
      slide.inert = !current;
      slide.classList.toggle('is-current', current);
      if (!current) {
        Array.prototype.forEach.call(slide.querySelectorAll('video, audio'), function (media) {
          media.pause();
        });
      }
    });
    Array.prototype.forEach.call(this.dots, function (dot, dotIndex) {
      if (dotIndex === index) {
        dot.setAttribute('aria-current', 'true');
      }
      else {
        dot.removeAttribute('aria-current');
      }
    });
    if (this.counter) {
      this.counter.textContent = (index + 1) + ' / ' + count;
    }
    if (announce && this.status) {
      this.status.textContent = Backdrop.t('Slide @index of @count', { '@index': index + 1, '@count': count });
    }
  };

})(Backdrop);
