<?php
/**
 * @file
 * Shows photos and videos one at a time, as a carousel.
 *
 * It follows the WAI-ARIA Authoring Practices carousel pattern. Without
 * JavaScript, the slides can still be scrolled through sideways. The script
 * shows the buttons and dots, and keeps them and the slides in step.
 *
 * Available variables:
 * - $items: The slides. Each is an array with these keys:
 *   - type: "image", "video", or "file".
 *   - markup: The HTML of the photo, video, or file link.
 *   - id: An ID for the slide.
 *   - label: The slide's position, such as "2 of 5", for screen readers.
 * - $id: An ID for the carousel.
 * - $count: The number of slides.
 * - $single: Whether there is only one slide, so no carousel controls.
 * - $show_dots: Whether to show a dot for each slide.
 * - $label: The carousel's accessible name.
 * - $previous_icon, $next_icon: Icons for the buttons.
 * - $classes: An array of classes for the wrapper.
 * - $attributes: An array of other attributes for the wrapper.
 *
 * @see template_preprocess_moby_carousel()
 */
?>
<section id="<?php print $id; ?>" class="<?php print implode(' ', $classes); ?>"<?php print backdrop_attributes($attributes); ?>>
  <div class="moby-carousel-stage">
    <?php if (!$single): ?>
      <?php // The buttons come first, so screen reader users find them before the slides. ?>
      <div class="moby-carousel-buttons" hidden>
        <button type="button" class="moby-carousel-button moby-carousel-previous" aria-controls="<?php print $id; ?>-slides" aria-label="<?php print t('Previous slide'); ?>"><?php print $previous_icon; ?></button>
        <button type="button" class="moby-carousel-button moby-carousel-next" aria-controls="<?php print $id; ?>-slides" aria-label="<?php print t('Next slide'); ?>"><?php print $next_icon; ?></button>
      </div>
    <?php endif; ?>

    <div class="moby-carousel-viewport" id="<?php print $id; ?>-slides"<?php if (!$single): ?> tabindex="0"<?php endif; ?>>
      <?php foreach ($items as $item): ?>
        <div class="moby-carousel-slide moby-carousel-slide--<?php print $item['type']; ?>" id="<?php print $item['id']; ?>"<?php if (!$single): ?> role="group" aria-roledescription="<?php print t('slide'); ?>" aria-label="<?php print $item['label']; ?>"<?php endif; ?>>
          <?php print $item['markup']; ?>
        </div>
      <?php endforeach; ?>
    </div>
  </div>

  <?php if ($show_dots): ?>
    <div class="moby-carousel-dots" hidden>
      <?php foreach ($items as $index => $item): ?>
        <button type="button" class="moby-carousel-dot" aria-controls="<?php print $item['id']; ?>" aria-label="<?php print t('Slide @index', array('@index' => $index + 1)); ?>"<?php if (!$index): ?> aria-current="true"<?php endif; ?>></button>
      <?php endforeach; ?>
    </div>
  <?php elseif (!$single): ?>
    <?php // Too many slides for dots: show the position as text instead. ?>
    <div class="moby-carousel-counter" aria-hidden="true" hidden>1 / <?php print $count; ?></div>
  <?php endif; ?>

  <?php if (!$single): ?>
    <div class="moby-carousel-status element-invisible" aria-live="polite" aria-atomic="true"></div>
  <?php endif; ?>
</section>
